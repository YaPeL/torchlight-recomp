#include "live/ui_gamepad.h"

#include <algorithm>

namespace torchlight::live {

namespace {
// Stick values below this are noise (resting pads drift).
constexpr float kDeadZone = 0.25f;
// The stick as a d-pad direction.
constexpr float kStickPress = 0.5f;

float Past(float value) { return value > kDeadZone ? std::min(value, 1.0f) : 0.0f; }
}  // namespace

std::vector<UiGamepadKey> GamepadToImGui(const platform::GamepadState& pad) {
  auto button = [](ImGuiKey key, bool down) { return UiGamepadKey{key, down, down ? 1.0f : 0.0f}; };
  return {
      button(ImGuiKey_GamepadFaceUp, pad.a),
      button(ImGuiKey_GamepadFaceDown, pad.y),
      button(ImGuiKey_GamepadFaceRight, pad.b),
      button(ImGuiKey_GamepadFaceLeft, pad.x),
      button(ImGuiKey_GamepadStart, pad.start),
      button(ImGuiKey_GamepadBack, pad.back),
      button(ImGuiKey_GamepadDpadUp, pad.up || -pad.left_y > kStickPress),
      button(ImGuiKey_GamepadDpadDown, pad.down || pad.left_y > kStickPress),
      button(ImGuiKey_GamepadDpadLeft, pad.left || -pad.left_x > kStickPress),
      button(ImGuiKey_GamepadDpadRight, pad.right || pad.left_x > kStickPress),
  };
}

bool GamepadHeld(const platform::GamepadState& pad) {
  return pad.a || pad.b || pad.x || pad.y || pad.start || pad.back || pad.up || pad.down ||
         pad.left || pad.right || Past(pad.left_x) > 0 || Past(-pad.left_x) > 0 ||
         Past(pad.left_y) > 0 || Past(-pad.left_y) > 0;
}

bool BlockGuestInput(bool dialogs_open, bool was_blocked, const platform::GamepadState& pad) {
  return dialogs_open || (was_blocked && GamepadHeld(pad));
}

}  // namespace torchlight::live
