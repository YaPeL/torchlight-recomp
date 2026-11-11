// One player, one controller at a time (one_player_input.h), with fake pads: the pad that last
// started an input is the only one read and the only one that rumbles; an idle pad never mixes
// into it; and, through the SDK's InputSystem, "Begin" pressed on the second pad reaches guest
// user 0 while user 1 has no controller (issue #138's Sign In Error).

#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <rex/input/device_assignment.h>
#include <rex/input/input_system.h>

#include "live/one_player_input.h"

namespace {

using rex::X_RESULT;
using rex::X_STATUS;
using rex::input::DeviceId;
using rex::input::DeviceInfo;
using rex::input::X_INPUT_GAMEPAD;
using torchlight::live::ActivePadDriver;

constexpr uint16_t kButtonA = 0x1000;
constexpr uint16_t kButtonX = 0x4000;

void Check(bool ok, const std::string& what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    std::exit(1);
  }
}

// Pads 1 and 2, as SDL lists them; each holds what its player is doing and the last rumble.
class FakePads final : public rex::input::InputDriver {
 public:
  FakePads() : InputDriver(nullptr, 0) {}

  X_STATUS Setup() override { return X_STATUS_SUCCESS; }
  void EnumerateDevices(std::vector<DeviceInfo>& out) override {
    for (const auto& [id, pad] : pads) {
      DeviceInfo info;
      info.id = DeviceId(id);
      info.name = "pad " + std::to_string(id);
      out.push_back(info);
    }
  }
  X_RESULT GetDeviceState(DeviceId id, rex::input::X_INPUT_STATE* out) override {
    auto it = pads.find(static_cast<uint64_t>(id));
    if (it == pads.end()) return X_ERROR_DEVICE_NOT_CONNECTED;
    *out = {};
    out->packet_number = ++it->second.packet;
    out->gamepad = it->second.now;
    return X_ERROR_SUCCESS;
  }
  X_RESULT GetDeviceCapabilities(DeviceId id, uint32_t,
                                 rex::input::X_INPUT_CAPABILITIES* caps) override {
    if (!pads.count(static_cast<uint64_t>(id))) return X_ERROR_DEVICE_NOT_CONNECTED;
    *caps = {};
    caps->gamepad.thumb_lx = 32767;
    caps->gamepad.thumb_ly = 32767;
    caps->gamepad.thumb_rx = 32767;
    caps->gamepad.thumb_ry = 32767;
    return X_ERROR_SUCCESS;
  }
  X_RESULT SetDeviceVibration(DeviceId id, rex::input::X_INPUT_VIBRATION* v) override {
    auto it = pads.find(static_cast<uint64_t>(id));
    if (it == pads.end()) return X_ERROR_DEVICE_NOT_CONNECTED;
    it->second.rumble = static_cast<uint16_t>(v->left_motor_speed);
    return X_ERROR_SUCCESS;
  }
  X_RESULT GetDeviceKeystroke(DeviceId, uint32_t, rex::input::X_INPUT_KEYSTROKE*) override {
    return X_ERROR_EMPTY;
  }

  struct Pad {
    X_INPUT_GAMEPAD now = {};
    uint32_t packet = 0;
    uint16_t rumble = 0;
  };
  std::map<uint64_t, Pad> pads = {{1, {}}, {2, {}}};
};

X_INPUT_GAMEPAD Stick(int16_t x, int16_t y) {
  X_INPUT_GAMEPAD g = {};
  g.thumb_lx = x;
  g.thumb_ly = y;
  return g;
}

X_INPUT_GAMEPAD Buttons(uint16_t buttons) {
  X_INPUT_GAMEPAD g = {};
  g.buttons = buttons;
  return g;
}

rex::input::X_INPUT_STATE Read(ActivePadDriver& driver) {
  rex::input::X_INPUT_STATE state = {};
  Check(driver.GetDeviceState(ActivePadDriver::kDevice, &state) == X_ERROR_SUCCESS, "read");
  return state;
}

struct Rig {
  FakePads* pads;
  std::unique_ptr<ActivePadDriver> driver;
};

Rig MakeRig() {
  auto fake = std::make_unique<FakePads>();
  FakePads* raw = fake.get();
  std::vector<std::unique_ptr<rex::input::InputDriver>> drivers;
  drivers.push_back(std::move(fake));
  return {raw, std::make_unique<ActivePadDriver>(std::move(drivers))};
}

void OneDeviceForTheGame() {
  auto rig = MakeRig();
  std::vector<DeviceInfo> listed;
  rig.driver->EnumerateDevices(listed);
  Check(listed.size() == 1 && listed[0].id == ActivePadDriver::kDevice && !listed[0].synthetic,
        "two pads show as one physical device");
  Check(rig.driver->Active() == DeviceId(1), "before any input the first pad plays");
}

// One pad idle (drifting inside its dead zone), the other walking: the reading is exactly the
// walking pad's every frame, so the hero moves without the idle pad's drift shaking it.
void IdlePadNeverMixes() {
  auto rig = MakeRig();
  for (int frame = 0; frame < 30; ++frame) {
    const int16_t drift = (frame % 2) ? 3000 : -3000;  // under the 7849 dead zone
    rig.pads->pads[1].now = Stick(drift, -drift);
    const int16_t x = static_cast<int16_t>(12000 + frame * 500);
    rig.pads->pads[2].now = Stick(x, 20000);
    const auto state = Read(*rig.driver);
    Check(static_cast<int16_t>(state.gamepad.thumb_lx) == x &&
              static_cast<int16_t>(state.gamepad.thumb_ly) == 20000,
          "frame " + std::to_string(frame) + ": the walking pad's stick, unmixed");
  }
  Check(rig.driver->Active() == DeviceId(2), "the walking pad plays");
}

void ActivePadChanges() {
  auto rig = MakeRig();
  rig.pads->pads[2].now = Stick(20000, 0);
  Read(*rig.driver);
  Check(rig.driver->Active() == DeviceId(2), "pad 2 starts walking and plays");

  // Pad 1 presses X while pad 2 still holds its stick: pad 1 takes over, pad 2's stick is gone.
  rig.pads->pads[1].now = Buttons(kButtonX);
  auto state = Read(*rig.driver);
  Check(rig.driver->Active() == DeviceId(1), "a new button on pad 1 takes over");
  Check(static_cast<uint16_t>(state.gamepad.buttons) == kButtonX &&
            static_cast<int16_t>(state.gamepad.thumb_lx) == 0,
        "only pad 1 is read");

  // Holding is not starting: pad 2 keeps its stick out and does not come back.
  for (int i = 0; i < 5; ++i) Read(*rig.driver);
  Check(rig.driver->Active() == DeviceId(1), "a held stick does not take over");

  // Back in the dead zone and out again: pad 2 plays.
  rig.pads->pads[2].now = Stick(0, 0);
  Read(*rig.driver);
  rig.pads->pads[2].now = Stick(0, -20000);
  state = Read(*rig.driver);
  Check(rig.driver->Active() == DeviceId(2) &&
            static_cast<int16_t>(state.gamepad.thumb_ly) == -20000,
        "a stick leaving its dead zone takes over");

  // A trigger pulled takes over too.
  rig.pads->pads[1].now = {};
  Read(*rig.driver);
  rig.pads->pads[1].now.right_trigger = 200;
  Read(*rig.driver);
  Check(rig.driver->Active() == DeviceId(1), "a trigger pulled takes over");
}

// Steam Input's copy of the pad in hand: both pads read the same, and the player never flips.
void DuplicatedPadStays() {
  auto rig = MakeRig();
  for (int frame = 0; frame < 10; ++frame) {
    const auto same = (frame % 3 == 0) ? Buttons(kButtonA) : Stick(15000, 0);
    rig.pads->pads[1].now = same;
    rig.pads->pads[2].now = same;
    Read(*rig.driver);
    Check(rig.driver->Active() == DeviceId(1), "frame " + std::to_string(frame) + ": no flip");
  }
}

void RumbleOnlyOnTheActivePad() {
  auto rig = MakeRig();
  rig.pads->pads[2].now = Buttons(kButtonA);
  Read(*rig.driver);
  rex::input::X_INPUT_VIBRATION rumble = {};
  rumble.left_motor_speed = 40000;
  Check(rig.driver->SetDeviceVibration(ActivePadDriver::kDevice, &rumble) == X_ERROR_SUCCESS,
        "rumble accepted");
  Check(rig.pads->pads[2].rumble == 40000 && rig.pads->pads[1].rumble == 0,
        "only the active pad rumbles");

  // The player picks up pad 1 mid-rumble: pad 2 stops, pad 1 takes the rumble.
  rig.pads->pads[1].now = Buttons(kButtonA);
  Read(*rig.driver);
  Check(rig.pads->pads[1].rumble == 40000 && rig.pads->pads[2].rumble == 0,
        "the rumble moves with the player");
}

void PacketNumberMovesOnASwitch() {
  auto rig = MakeRig();
  const uint32_t before = Read(*rig.driver).packet_number;
  Check(Read(*rig.driver).packet_number == before, "nothing changed, same packet");
  rig.pads->pads[2].now = Buttons(kButtonA);
  Check(Read(*rig.driver).packet_number != before, "pad 2's press is a new packet");
}

void UnpluggedPadHandsOver() {
  auto rig = MakeRig();
  rig.pads->pads[2].now = Buttons(kButtonA);
  Read(*rig.driver);
  rig.pads->pads.erase(2);
  Read(*rig.driver);
  Check(rig.driver->Active() == DeviceId(1), "pad 2 unplugged, pad 1 plays");
  rig.pads->pads.clear();
  std::vector<DeviceInfo> listed;
  rig.driver->EnumerateDevices(listed);
  rex::input::X_INPUT_STATE state;
  Check(listed.empty() &&
            rig.driver->GetDeviceState(ActivePadDriver::kDevice, &state) ==
                X_ERROR_DEVICE_NOT_CONNECTED,
        "no pad, no device");
}

// Issue #138 through the SDK: "Begin" (A) on the second pad reaches user 0; user 1 has no
// controller, so the game never asks it to sign in. With the SDK's default (every pad its own
// device under SlotAssignment) the same press went to user 1.
void SecondPadBeginsAsUserZero() {
  auto rig = MakeRig();
  rex::input::InputSystem input(nullptr);
  input.AddDriver(std::move(rig.driver));
  input.SetDeviceAssignment(std::make_unique<rex::input::SlotAssignment>());

  rex::input::X_INPUT_STATE state = {};
  Check(input.GetState(0, &state) == X_ERROR_SUCCESS, "user 0 has a controller");
  rig.pads->pads[2].now = Buttons(kButtonA);
  Check(input.GetState(0, &state) == X_ERROR_SUCCESS &&
            static_cast<uint16_t>(state.gamepad.buttons) == kButtonA,
        "A on the second pad is user 0's A");
  for (uint32_t user = 1; user < rex::input::kMaxGuestUsers; ++user) {
    Check(input.GetState(user, &state) == X_ERROR_DEVICE_NOT_CONNECTED,
          "user " + std::to_string(user) + ": no controller");
  }
  input.Shutdown();
}

}  // namespace

int main() {
  OneDeviceForTheGame();
  IdlePadNeverMixes();
  ActivePadChanges();
  DuplicatedPadStays();
  RumbleOnlyOnTheActivePad();
  PacketNumberMovesOnASwitch();
  UnpluggedPadHandsOver();
  SecondPadBeginsAsUserZero();
  std::puts("one_player_input_test: ok");
  return 0;
}
