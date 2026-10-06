// The gamepad driving the host UI (the runtime's ImGui dialogs in only mode): its state as ImGui
// gamepad navigation keys, and whether the guest's input stays blocked.

#pragma once

#include <vector>

#include <imgui.h>

#include "platform/platform.h"

namespace torchlight::live {

struct UiGamepadKey {
  ImGuiKey key;
  bool down;
  float value;  // analog value (1 or 0 for buttons)
};

// The pad as ImGui keys, every key every frame (ImGui drops repeats):
// - A is ImGui's "input" activation (GamepadFaceUp): it presses buttons like the plain one and
//   also enters text fields, which the plain activation (PreferTweak) does not; Y is the plain
//   one. The dialogs hold buttons and text fields.
// - B cancels (ImGui's own cancel: leaves a field, closes a popup).
// - The d-pad and the left stick move the focus: ImGui moves it with the d-pad only (the stick
//   scrolls), so the stick is sent as d-pad presses past a threshold.
std::vector<UiGamepadKey> GamepadToImGui(const platform::GamepadState& pad);

// Whether any button or direction is held (stick past the dead zone).
bool GamepadHeld(const platform::GamepadState& pad);

// The guest's input is blocked while dialogs are open and, after they close, until the pad is
// released: the A that confirms a dialog must not reach the game as a new press.
bool BlockGuestInput(bool dialogs_open, bool was_blocked, const platform::GamepadState& pad);

}  // namespace torchlight::live
