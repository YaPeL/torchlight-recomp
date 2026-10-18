// The touch controls' virtual pad (docs/android-port.md, stage A4): the controls laid out on the
// screen in millimetres, and the fingers on them turned into the controller state the game reads.
// Pure: no SDL, no window; the driver (live/touch_input.h) feeds it the fingers.
//
// The finger rules (the owner's, 2026-10-17):
// - Each finger owns one control from the moment it lands.
// - A button stays pressed while its finger is on it: the 360 attacks while X is held. It is
//   released when the finger lifts or leaves it (past kHoldSlop times its radius, so a thumb that
//   rolls a little does not drop the attack), as in the Rayman recomp for Android.
// - A finger that was on a button presses another one it slides onto (and the same one again if it
//   comes back); a finger that landed between controls never presses anything.
// - The stick: a finger that lands in the stick's zone (the lower left, off the buttons) puts the
//   stick under it and keeps it until it lifts, wherever it goes.
// - Several fingers at once: the state is the union of all of them (move, attack and drink at the
//   same time).

#pragma once

#include <cstdint>
#include <map>
#include <string_view>
#include <vector>

#include "input/pad_state.h"

namespace torchlight::input {

// The screen in pixels and its density.
struct TouchScreen {
  float width = 0, height = 0;  // pixels
  float px_per_mm = 0;
};

// A round control, in pixels.
struct TouchButton {
  std::string_view name;
  uint16_t buttons = 0;  // pad::k* bits
  bool left_trigger = false, right_trigger = false;
  float x = 0, y = 0, radius = 0;
};

struct TouchLayout {
  std::vector<TouchButton> buttons;
  // The stick: where its base rests while no finger holds it, its radius, and the zone a finger
  // lands in to take it (x0, y0, x1, y1: pixels, y down).
  float stick_x = 0, stick_y = 0, stick_radius = 0;
  float zone_x0 = 0, zone_y0 = 0, zone_x1 = 0, zone_y1 = 0;
};

// The sizes, in millimetres (docs/android-port.md, A4: Android's 48 dp target is about 9 mm).
namespace touch_mm {
constexpr float kAttack = 16, kSkill = 11, kSmall = 9;  // diameters
constexpr float kStickRadius = 13;
constexpr float kSideMargin = 6;   // left and right edges
constexpr float kLongMargin = 8;   // top and bottom: the POCO's curved long edges in landscape
constexpr float kMinGap = 1.5f;    // Android's 8 dp between targets
}  // namespace touch_mm

// The gameplay layout (A4.1's; refined at A4.3): the stick at the bottom left; X (attack, the
// largest) at the bottom right with Y, B, LT and RT in an arc around it; A apart, left of the arc;
// LB and RB above the stick; Back and Start in the top corners.
TouchLayout GameplayLayout(const TouchScreen& screen);

// Past this many radii from its centre a held button lets go.
constexpr float kHoldSlop = 1.25f;

class TouchPad {
 public:
  explicit TouchPad(TouchLayout layout = {}) : layout_(std::move(layout)) {}

  // A new layout (the window changed): the fingers are dropped, as if lifted.
  void SetLayout(TouchLayout layout);
  const TouchLayout& layout() const { return layout_; }

  // Fingers by id, in pixels.
  void Down(uint64_t finger, float x, float y);
  void Move(uint64_t finger, float x, float y);
  void Up(uint64_t finger);
  // Every finger lifted (the controls hid).
  void Clear() { fingers_.clear(); }

  PadState State() const;
  // Where the stick's base and knob are now (the resting base while no finger holds it).
  float stick_base_x() const;
  float stick_base_y() const;
  bool stick_held() const;

 private:
  enum class Role { kNone, kButton, kLostButton, kStick };
  struct Finger {
    Role role = Role::kNone;
    int button = -1;          // kButton
    float base_x = 0, base_y = 0;  // kStick: where the stick's base is
    float x = 0, y = 0;
  };
  int ButtonAt(float x, float y) const;

  TouchLayout layout_;
  std::map<uint64_t, Finger> fingers_;
};

}  // namespace torchlight::input
