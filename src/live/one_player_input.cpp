#include "live/one_player_input.h"

#include <cstdlib>
#include <cstring>

#include <rex/input/flags.h>
#include <rex/input/input_system.h>
#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/input/nop/nop_input_driver.h>
#include <rex/input/sdl/sdl_input_driver.h>
#include <rex/input/state_merge.h>
#include <rex/logging.h>
#include <rex/platform.h>
#if REX_PLATFORM_WIN32
#include <rex/input/xinput/xinput_input_driver.h>
#endif

namespace torchlight::live {

namespace {

using rex::X_RESULT;
using rex::X_STATUS;
using rex::input::DeviceId;
using rex::input::X_INPUT_GAMEPAD;

bool PastDeadzone(int16_t axis) {
  return std::abs(static_cast<int>(axis)) >= rex::input::kThumbDeadzone;
}

bool StickOut(int16_t x, int16_t y) {
  return PastDeadzone(x) || PastDeadzone(y);
}

bool TriggerIn(uint8_t trigger) {
  return trigger >= rex::input::kTriggerThreshold;
}

// A new button, or a stick or trigger that just left its dead zone. Holding an input is not
// starting one, so two pads both held (Steam Input's copy of the pad in hand) never trade places.
bool StartedInput(const X_INPUT_GAMEPAD& before, const X_INPUT_GAMEPAD& now) {
  const auto pressed = static_cast<uint16_t>(now.buttons) & ~static_cast<uint16_t>(before.buttons);
  if (pressed != 0) return true;
  if (TriggerIn(now.left_trigger) && !TriggerIn(before.left_trigger)) return true;
  if (TriggerIn(now.right_trigger) && !TriggerIn(before.right_trigger)) return true;
  if (StickOut(now.thumb_lx, now.thumb_ly) && !StickOut(before.thumb_lx, before.thumb_ly)) {
    return true;
  }
  return StickOut(now.thumb_rx, now.thumb_ry) && !StickOut(before.thumb_rx, before.thumb_ry);
}

}  // namespace

ActivePadDriver::ActivePadDriver(std::vector<std::unique_ptr<rex::input::InputDriver>> pads)
    : InputDriver(nullptr, 0), drivers_(std::move(pads)) {
  // The SDK hands its window-focus test to the drivers it holds, which is this one.
  for (auto& driver : drivers_) {
    driver->set_is_active_callback([this] { return is_active(); });
  }
}

X_STATUS ActivePadDriver::Setup() {
  return X_STATUS_SUCCESS;
}

void ActivePadDriver::OnWindowAvailable(rex::ui::Window* window) {
  for (auto& driver : drivers_) driver->OnWindowAvailable(window);
}

DeviceId ActivePadDriver::Active() const {
  std::lock_guard lock(mutex_);
  return active_;
}

void ActivePadDriver::RefreshPadsLocked() {
  std::vector<Pad> pads;
  std::vector<rex::input::DeviceInfo> listed;
  for (auto& driver : drivers_) {
    listed.clear();
    driver->EnumerateDevices(listed);
    for (const auto& info : listed) {
      if (info.synthetic) continue;
      Pad pad;
      pad.driver = driver.get();
      pad.info = info;
      for (const auto& old : pads_) {
        if (old.driver == pad.driver && old.info.id == info.id) pad.last = old.last;
      }
      pads.push_back(std::move(pad));
    }
  }
  pads_ = std::move(pads);
  if (!ActivePadLocked()) {
    active_ = DeviceId::kInvalid;
    active_driver_ = nullptr;
    if (!pads_.empty()) SwitchToLocked(pads_.front());
  }
}

ActivePadDriver::Pad* ActivePadDriver::ActivePadLocked() {
  for (auto& pad : pads_) {
    if (pad.driver == active_driver_ && pad.info.id == active_) return &pad;
  }
  return nullptr;
}

void ActivePadDriver::SwitchToLocked(Pad& pad) {
  if (Pad* old = ActivePadLocked()) {
    rex::input::X_INPUT_VIBRATION stop = {};
    old->driver->SetDeviceVibration(old->info.id, &stop);
  }
  const bool first = active_ == DeviceId::kInvalid;
  active_ = pad.info.id;
  active_driver_ = pad.driver;
  if (static_cast<uint16_t>(vibration_.left_motor_speed) != 0 ||
      static_cast<uint16_t>(vibration_.right_motor_speed) != 0) {
    rex::input::X_INPUT_VIBRATION rumble = vibration_;
    pad.driver->SetDeviceVibration(pad.info.id, &rumble);
  }
  REXLOG_INFO("input: {} \"{}\" plays the one player", first ? "controller" : "now controller",
              pad.info.name);
}

bool ActivePadDriver::PollLocked(rex::input::X_INPUT_STATE& out) {
  RefreshPadsLocked();
  Pad* active = ActivePadLocked();
  if (!active) return false;

  Pad* claimant = nullptr;
  bool active_started = false;
  X_INPUT_GAMEPAD active_now = {};
  for (auto& pad : pads_) {
    rex::input::X_INPUT_STATE state = {};
    if (pad.driver->GetDeviceState(pad.info.id, &state) != X_ERROR_SUCCESS) continue;
    const bool started = StartedInput(pad.last, state.gamepad);
    pad.last = state.gamepad;
    if (&pad == active) {
      active_started = started;
      active_now = state.gamepad;
    } else if (started && !claimant) {
      claimant = &pad;
    }
  }
  // The pad in hand keeps the player while it is starting inputs too (two pads pressed at once).
  if (claimant && !active_started) {
    SwitchToLocked(*claimant);
    active_now = claimant->last;
  }

  out = {};
  out.gamepad = active_now;
  if (std::memcmp(&active_now, &last_out_, sizeof(active_now)) != 0) {
    ++packet_number_;
    last_out_ = active_now;
  }
  out.packet_number = packet_number_;
  return true;
}

void ActivePadDriver::EnumerateDevices(std::vector<rex::input::DeviceInfo>& out) {
  std::lock_guard lock(mutex_);
  RefreshPadsLocked();
  const Pad* active = ActivePadLocked();
  if (!active) return;
  rex::input::DeviceInfo info = active->info;
  info.id = kDevice;
  info.ordinal = 0;
  info.synthetic = false;
  out.push_back(std::move(info));
}

X_RESULT ActivePadDriver::GetDeviceState(DeviceId id, rex::input::X_INPUT_STATE* out_state) {
  if (id != kDevice || !out_state) return X_ERROR_DEVICE_NOT_CONNECTED;
  std::lock_guard lock(mutex_);
  return PollLocked(*out_state) ? X_ERROR_SUCCESS : X_ERROR_DEVICE_NOT_CONNECTED;
}

X_RESULT ActivePadDriver::GetDeviceCapabilities(DeviceId id, uint32_t flags,
                                                rex::input::X_INPUT_CAPABILITIES* out_caps) {
  if (id != kDevice) return X_ERROR_DEVICE_NOT_CONNECTED;
  std::lock_guard lock(mutex_);
  Pad* active = ActivePadLocked();
  if (!active) return X_ERROR_DEVICE_NOT_CONNECTED;
  return active->driver->GetDeviceCapabilities(active->info.id, flags, out_caps);
}

X_RESULT ActivePadDriver::SetDeviceVibration(DeviceId id,
                                             rex::input::X_INPUT_VIBRATION* vibration) {
  if (id != kDevice || !vibration) return X_ERROR_DEVICE_NOT_CONNECTED;
  std::lock_guard lock(mutex_);
  vibration_ = *vibration;
  Pad* active = ActivePadLocked();
  if (!active) return X_ERROR_DEVICE_NOT_CONNECTED;
  return active->driver->SetDeviceVibration(active->info.id, vibration);
}

X_RESULT ActivePadDriver::GetDeviceKeystroke(DeviceId id, uint32_t flags,
                                             rex::input::X_INPUT_KEYSTROKE* out_keystroke) {
  if (id != kDevice) return X_ERROR_DEVICE_NOT_CONNECTED;
  std::lock_guard lock(mutex_);
  rex::input::X_INPUT_STATE ignored;
  if (!PollLocked(ignored)) return X_ERROR_DEVICE_NOT_CONNECTED;
  // The other pads' keystrokes are dropped as they come, so one never arrives late, after its pad
  // took over.
  for (auto& pad : pads_) {
    if (pad.driver == active_driver_ && pad.info.id == active_) continue;
    rex::input::X_INPUT_KEYSTROKE dropped;
    for (int i = 0; i < 32; ++i) {
      if (pad.driver->GetDeviceKeystroke(pad.info.id, flags, &dropped) != X_ERROR_SUCCESS) break;
    }
  }
  Pad* active = ActivePadLocked();
  return active->driver->GetDeviceKeystroke(active->info.id, flags, out_keystroke);
}

std::unique_ptr<rex::system::IInputSystem> CreateOnePlayerInputSystem(bool tool_mode) {
  auto input = std::make_unique<rex::input::InputSystem>(nullptr);

  if (!tool_mode) {
    std::vector<std::unique_ptr<rex::input::InputDriver>> pads;
#if REX_PLATFORM_WIN32
    if (REXCVAR_GET(input_backend) == "xinput") {
      auto xinput_driver = std::make_unique<rex::input::xinput::XinputInputDriver>(nullptr, 0);
      if (xinput_driver->Setup() == X_STATUS_SUCCESS) pads.push_back(std::move(xinput_driver));
    }
#endif
    if (REXCVAR_GET(input_backend) == "sdl") {
      auto sdl_driver = std::make_unique<rex::input::sdl::SDLInputDriver>(nullptr, 0);
      if (sdl_driver->Setup() == X_STATUS_SUCCESS) pads.push_back(std::move(sdl_driver));
    }
    if (!pads.empty()) input->AddDriver(std::make_unique<ActivePadDriver>(std::move(pads)));

    auto mnk_driver = std::make_unique<rex::input::mnk::MnkInputDriver>(nullptr, 0);
    if (mnk_driver->Setup() == X_STATUS_SUCCESS) input->AddDriver(std::move(mnk_driver));
  }

  const uint8_t nop_index = tool_mode ? 0 : 1;
  input->AddDriver(std::make_unique<rex::input::nop::NopInputDriver>(nullptr, nop_index));
  input->SetDeviceAssignment(std::make_unique<rex::input::SlotAssignment>());
  REXLOG_INFO("input: one player, one controller at a time (the last one to start an input)");
  return input;
}

}  // namespace torchlight::live
