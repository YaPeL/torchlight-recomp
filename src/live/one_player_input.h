// One player, one controller at a time (issue #138, 2026-11-10). The SDK gives controller N to
// guest user N by default (SlotAssignment, since its July 2026 input rework); Torchlight is single
// player and only user 0 is signed in, so "Begin" pressed on a second controller (two pads
// connected, or a pad plus a virtual one such as Steam Input's) made the game ask user 1 for a
// gamer profile: "Sign In Error ... You must be signed in with a gamer profile", with no way past
// it.
//
// The physical pads are wrapped in one device, which the SDK then gives to user 0 (a lone physical
// device always gets the lowest ordinal). The pad that speaks for it is the active one: the last
// pad to start an input (a new button, or a stick or trigger leaving its dead zone). Only it is
// read and only it rumbles, so an idle pad, or Steam Input's copy of the same pad, never mixes
// into the player's input. Synthetic devices (keyboard and mouse, the touch controls, a script's
// controller) are not pads and still feed user 0 beside it.

#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <rex/input/input_driver.h>

namespace rex::system {
class IInputSystem;
}  // namespace rex::system

namespace torchlight::live {

class ActivePadDriver final : public rex::input::InputDriver {
 public:
  // The one device this driver shows, while any pad is connected.
  static constexpr rex::input::DeviceId kDevice = static_cast<rex::input::DeviceId>(0x41435450);

  // The pad drivers (SDL, XInput), already set up.
  explicit ActivePadDriver(std::vector<std::unique_ptr<rex::input::InputDriver>> pads);

  rex::X_STATUS Setup() override;
  void EnumerateDevices(std::vector<rex::input::DeviceInfo>& out) override;
  rex::X_RESULT GetDeviceState(rex::input::DeviceId id,
                               rex::input::X_INPUT_STATE* out_state) override;
  rex::X_RESULT GetDeviceCapabilities(rex::input::DeviceId id, uint32_t flags,
                                      rex::input::X_INPUT_CAPABILITIES* out_caps) override;
  rex::X_RESULT SetDeviceVibration(rex::input::DeviceId id,
                                   rex::input::X_INPUT_VIBRATION* vibration) override;
  rex::X_RESULT GetDeviceKeystroke(rex::input::DeviceId id, uint32_t flags,
                                   rex::input::X_INPUT_KEYSTROKE* out_keystroke) override;
  void OnWindowAvailable(rex::ui::Window* window) override;

  // The pad in the player's hands (kInvalid with no pad).
  rex::input::DeviceId Active() const;

 private:
  struct Pad {
    rex::input::InputDriver* driver = nullptr;
    rex::input::DeviceInfo info;
    rex::input::X_INPUT_GAMEPAD last = {};
  };

  // Re-lists the pads, keeping each one's last reading; with the active pad gone, the first pad
  // takes over.
  void RefreshPadsLocked();
  // Reads every pad, moves the active one to a pad that started an input, and gives the active
  // pad's reading (false with no pad).
  bool PollLocked(rex::input::X_INPUT_STATE& out);
  Pad* ActivePadLocked();
  // The rumble moves with the player: the old pad stops, the new one takes the guest's last one.
  void SwitchToLocked(Pad& pad);

  std::vector<std::unique_ptr<rex::input::InputDriver>> drivers_;
  mutable std::mutex mutex_;
  std::vector<Pad> pads_;
  rex::input::DeviceId active_ = rex::input::DeviceId::kInvalid;
  rex::input::InputDriver* active_driver_ = nullptr;
  rex::input::X_INPUT_VIBRATION vibration_ = {};
  // Ours, not the active pad's: a switch must never read as "no change" to the game.
  uint32_t packet_number_ = 0;
  rex::input::X_INPUT_GAMEPAD last_out_ = {};
};

// The SDK's CreateDefaultInputSystem with its pad drivers behind an ActivePadDriver (the same
// drivers, in the same order, chosen by the same input_backend setting).
std::unique_ptr<rex::system::IInputSystem> CreateOnePlayerInputSystem(bool tool_mode);

}  // namespace torchlight::live
