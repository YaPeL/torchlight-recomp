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

// A control's shape: round, wide (a pill), or the d-pad (one round cross whose direction comes from
// where the finger is on it).
enum class TouchShape { kRound, kWide, kDpad };

// A control, in pixels: round ones have `radius`; wide ones are `half_width` across and `radius`
// up and down from their centre.
struct TouchButton {
  std::string_view name;
  uint16_t buttons = 0;  // pad::k* bits
  bool left_trigger = false, right_trigger = false;
  float x = 0, y = 0, radius = 0;
  TouchShape shape = TouchShape::kRound;
  float half_width = 0;  // kWide
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
constexpr float kFace = 10, kSmall = 9;  // diameters: A, B, X, Y; the rest
constexpr float kShoulderWidth = 14, kMenuWidth = 12;  // the wide ones (kSmall tall)
constexpr float kStickRadius = 11;
constexpr float kFaceSpread = 10;  // the face buttons' centres from the diamond's centre
constexpr float kDpadRadius = 8;   // the d-pad's cross, half its width (the owner: much smaller
                                   // than four 9 mm buttons)
constexpr float kSideMargin = 6;   // left and right edges
constexpr float kLongMargin = 8;   // the bottom: the POCO's curved long edge in landscape
constexpr float kTopRowMargin = 2;  // the top row (the shoulders, Back, Start): almost at the top,
                                    // so they hide less of the picture (the owner, 2026-10-18)
constexpr float kMinGap = 1.5f;    // Android's 8 dp between targets
}  // namespace touch_mm

// The gameplay layout, a pad's (the owner's choice, 2026-10-18, after an emulator's on a phone):
// - the stick on the left, as high as mid height (where that pad has its d-pad; moving is what
//   matters most here), floating;
// - a small d-pad below and inward of it (the skill set; the menus): one cross, 8 directions;
// - A, B, X and Y on the right, as high as mid height, in the pad's diamond;
// (as high as the shoulders' row leaves room for: the owner wanted them higher, 2026-10-18)
// - the shoulders in the top corners, wide: LT and LB on the left, RB and RT on the right;
// - Back and Start at the top centre, wide.
// It fits screens at least kMinHeightMm tall and kMinWidthMm wide in landscape (phones and tablets
// are: a 20:9 phone about 57-60 x 127-132 mm, a 16:9 one at 57 mm about 101 mm wide).
TouchLayout GameplayLayout(const TouchScreen& screen);

// The d-pad's directions (pad::kUp...) for a point on it: by its angle from the centre, a
// diagonal pressing both sides (one component counts when it is at least half the other); none
// within a fifth of the radius of the centre.
uint16_t DpadDirections(const TouchButton& dpad, float x, float y);

constexpr float kMinHeightMm = 57;  // the shortest devices expected; the geometry fits down to ~55
constexpr float kMinWidthMm = 100;

// Past this many radii from its centre a held button lets go (a wide one: past a quarter of its
// height's half beyond its box).
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
  // Whether a finger is on the screen as far as the pad knows (a new layout drops them).
  bool tracks(uint64_t finger) const { return fingers_.contains(finger); }

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
