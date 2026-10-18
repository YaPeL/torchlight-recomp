// When the touch controls are shown (docs/android-port.md, A4; the owner's decisions of 2026-10-10
// and 2026-10-17). The setting:
// - Auto (the default): shown when the game starts with no pad and the system has a touch screen;
//   hidden as soon as a physical pad is used, and shown again as soon as the screen is touched,
//   even with the pad still connected. With no touch screen (a desktop) they never show.
// - Always: shown, whatever pads there are.
// - Never: never shown (the Android handhelds with a pad built in, where a touch on the screen must
//   not bring them up).
// The touch that brings them back only shows them: its finger presses nothing until it lifts, so
// reaching for the screen never fires an attack.

#pragma once

namespace torchlight::input {

enum class TouchMode { kAuto, kAlways, kNever };

class TouchVisibility {
 public:
  TouchVisibility(TouchMode mode, bool pad_at_start, bool has_touch_screen);

  bool visible() const { return visible_; }
  // A finger landed: whether it may act on the controls (they were already shown). In Auto it also
  // shows them.
  bool OnFingerDown();
  // A physical pad was used (a button, or an axis past its dead zone): in Auto it hides them.
  // Returns whether they were hidden by it.
  bool OnPadUsed();

 private:
  TouchMode mode_;
  bool has_touch_screen_;
  bool visible_;
};

}  // namespace torchlight::input
