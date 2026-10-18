#include "input/touch_visibility.h"

namespace torchlight::input {

TouchVisibility::TouchVisibility(TouchMode mode, bool pad_at_start, bool has_touch_screen)
    : mode_(mode), has_touch_screen_(has_touch_screen) {
  switch (mode) {
    case TouchMode::kAlways:
      visible_ = true;
      break;
    case TouchMode::kNever:
      visible_ = false;
      break;
    case TouchMode::kAuto:
      visible_ = has_touch_screen && !pad_at_start;
      break;
  }
}

bool TouchVisibility::OnFingerDown() {
  if (visible_) return true;
  if (mode_ == TouchMode::kAuto && has_touch_screen_) visible_ = true;
  return false;
}

bool TouchVisibility::OnPadUsed() {
  if (mode_ != TouchMode::kAuto || !visible_) return false;
  visible_ = false;
  return true;
}

}  // namespace torchlight::input
