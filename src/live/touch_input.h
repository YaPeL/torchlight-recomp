// The touch controls' input (docs/android-port.md, A4.1): the fingers on the screen as a controller
// of its own that the SDK gives guest user 0 together with any real pad (its state merge ORs them),
// laid out and read by input/touch_pad.h, shown or hidden by input/touch_visibility.h. Finger
// events arrive through an SDL event watch (on Android, on the Java side's thread): the watch only
// stores them under a lock; the game reads the state on its input poll. Drawing them is A4.2.

#pragma once

#include <cstdint>
#include <string_view>

#include "input/pad_state.h"
#include "input/touch_pad.h"
#include "settings/host_settings.h"

namespace rex {
class Runtime;
}

namespace torchlight::live {

// Before the guest runs (with the other input drivers), the game window open. Installs nothing with
// "never".
void InstallTouchControls(rex::Runtime* runtime, settings::TouchControls mode);

// What the drawing needs (A4.2): whether they are shown, the layout and what is pressed.
struct TouchSnapshot {
  bool visible = false;
  input::TouchLayout layout;
  input::PadState state;
  float stick_base_x = 0, stick_base_y = 0;
  bool stick_held = false;
  float width = 0, height = 0;  // the window's pixels the layout is for
};
// False when no touch controls are installed.
bool ReadTouchControls(TouchSnapshot& out);

// Development (the input scripts' touch commands, dev/script_input.cpp): a finger by id at a
// position in window fractions (0..1), down or moved; `up` lifts it. As if it came from the screen.
void InjectTouch(uint64_t finger, float x, float y, bool up);
// Development: the centre of the touch control named `name` (ignoring case: "X", "LB", "Back"...)
// in window fractions; false when there is none.
bool TouchControlCentre(std::string_view name, float& x, float& y);

}  // namespace torchlight::live
