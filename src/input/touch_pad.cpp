#include "input/touch_pad.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace torchlight::input {
namespace {

float Distance(float x0, float y0, float x1, float y1) { return std::hypot(x1 - x0, y1 - y0); }

}  // namespace

TouchLayout GameplayLayout(const TouchScreen& screen) {
  using namespace touch_mm;
  const float mm = screen.px_per_mm;
  const float w = screen.width, h = screen.height;
  const float h_mm = mm > 0 ? h / mm : 0;
  // From an edge, in millimetres, to pixels (y down).
  const auto left = [&](float v) { return v * mm; };
  const auto right = [&](float v) { return w - v * mm; };
  const auto top = [&](float v) { return v * mm; };
  const auto bottom = [&](float v) { return h - v * mm; };
  const float small_r = kSmall / 2, face_r = kFace / 2;
  TouchLayout layout;
  const auto round = [&](std::string_view name, uint16_t bits, float x, float y, float r, TouchShape shape) {
    layout.buttons.push_back({name, bits, false, false, x, y, r * mm, shape, 0});
  };
  const auto wide = [&](std::string_view name, uint16_t bits, bool lt, bool rt, float x, float y, float width) {
    layout.buttons.push_back({name, bits, lt, rt, x, y, small_r * mm, TouchShape::kWide, width / 2 * mm});
  };

  // The shoulders' row ends here (from the top); what is below it keeps the gap.
  const float below_row = kTopRowMargin + kSmall + kMinGap;

  // A, B, X, Y: the pad's diamond, as high as mid height, below the shoulders, above the margin.
  const float face_x = kSideMargin + face_r + kFaceSpread;
  const float face_y = std::max(std::min(0.5f * h_mm, h_mm - below_row - kFaceSpread - face_r),
                                kLongMargin + face_r + kFaceSpread);
  round("A", pad::kA, right(face_x), bottom(face_y - kFaceSpread), face_r, TouchShape::kRound);
  round("B", pad::kB, right(face_x - kFaceSpread), bottom(face_y), face_r, TouchShape::kRound);
  round("X", pad::kX, right(face_x + kFaceSpread), bottom(face_y), face_r, TouchShape::kRound);
  round("Y", pad::kY, right(face_x), bottom(face_y + kFaceSpread), face_r, TouchShape::kRound);

  // The stick's resting place: as high as mid height on the left, below the shoulders.
  const float stick_x = kSideMargin + kStickRadius + 3;
  const float stick_y = std::max(std::min(0.5f * h_mm, h_mm - below_row - kStickRadius),
                                 kLongMargin + kStickRadius + 3);
  layout.stick_x = left(stick_x);
  layout.stick_y = bottom(stick_y);
  layout.stick_radius = kStickRadius * mm;

  // The d-pad: on the bottom margin, tucked under the stick and inward of it, the gap between them.
  const float dpad_y = kLongMargin + kDpadRadius;
  const float reach = kStickRadius + kDpadRadius + kMinGap, dy = stick_y - dpad_y;
  // At least a stick's radius inward, so on a tall screen (a tablet) it is not under the stick.
  const float dpad_x = stick_x + std::max(std::sqrt(std::max(0.0f, reach * reach - dy * dy)), kStickRadius);
  round("D-pad", 0, left(dpad_x), bottom(dpad_y), kDpadRadius, TouchShape::kDpad);

  // The shoulders in the top corners, and Back and Start at the top centre.
  const float row = kTopRowMargin + small_r;
  const float outer = kSideMargin + kShoulderWidth / 2, inner = outer + kShoulderWidth + kMinGap;
  wide("LT", 0, true, false, left(outer), top(row), kShoulderWidth);
  wide("LB", pad::kLeftShoulder, false, false, left(inner), top(row), kShoulderWidth);
  wide("RB", pad::kRightShoulder, false, false, right(inner), top(row), kShoulderWidth);
  wide("RT", 0, false, true, right(outer), top(row), kShoulderWidth);
  const float centre = (kMenuWidth + kMinGap) / 2 * mm;
  wide("Back", pad::kBack, false, false, w / 2 - centre, top(row), kMenuWidth);
  wide("Start", pad::kStart, false, false, w / 2 + centre, top(row), kMenuWidth);

  // The stick's zone: the lower left 40% x 70% of the screen (a button there takes the touch).
  layout.zone_x0 = 0;
  layout.zone_y0 = h * 0.3f;
  layout.zone_x1 = w * 0.4f;
  layout.zone_y1 = h;
  return layout;
}

void TouchPad::SetLayout(TouchLayout layout) {
  layout_ = std::move(layout);
  fingers_.clear();
}

// How far a point is into a control, 0 at its centre and 1 at its edge (a wide one's box).
static float Reach(const TouchButton& b, float x, float y) {
  if (b.shape == TouchShape::kWide && b.half_width > 0) {
    return std::max(std::abs(x - b.x) / b.half_width, std::abs(y - b.y) / b.radius);
  }
  return Distance(x, y, b.x, b.y) / b.radius;
}

// Whether a point is within a control grown by `slop`: a round one's radius times it; a wide one by
// (slop - 1) times its height's half on every side, so its ends grow as little as its sides.
static bool Inside(const TouchButton& b, float x, float y, float slop) {
  if (b.shape == TouchShape::kWide && b.half_width > 0) {
    const float grow = (slop - 1) * b.radius;
    return std::abs(x - b.x) <= b.half_width + grow && std::abs(y - b.y) <= b.radius + grow;
  }
  return Distance(x, y, b.x, b.y) <= b.radius * slop;
}

uint16_t DpadDirections(const TouchButton& dpad, float x, float y) {
  const float dx = x - dpad.x, dy = y - dpad.y;
  if (std::hypot(dx, dy) < dpad.radius * 0.2f) return 0;
  uint16_t bits = 0;
  if (std::abs(dx) >= std::abs(dy) * 0.5f) bits |= dx > 0 ? pad::kRight : pad::kLeft;
  if (std::abs(dy) >= std::abs(dx) * 0.5f) bits |= dy > 0 ? pad::kDown : pad::kUp;
  return bits;
}

int TouchPad::ButtonAt(float x, float y) const {
  int best = -1;
  float best_reach = 0;
  for (int i = 0; i < static_cast<int>(layout_.buttons.size()); ++i) {
    const float reach = Reach(layout_.buttons[i], x, y);
    if (reach <= 1 && (best < 0 || reach < best_reach)) {
      best = i;
      best_reach = reach;
    }
  }
  return best;
}

void TouchPad::Down(uint64_t id, float x, float y) {
  Finger f;
  f.x = x;
  f.y = y;
  if (const int b = ButtonAt(x, y); b >= 0) {
    f.role = Role::kButton;
    f.button = b;
  } else if (!stick_held() && x >= layout_.zone_x0 && x <= layout_.zone_x1 && y >= layout_.zone_y0 &&
             y <= layout_.zone_y1) {
    f.role = Role::kStick;
    f.base_x = x;
    f.base_y = y;
  }
  fingers_[id] = f;
}

void TouchPad::Move(uint64_t id, float x, float y) {
  const auto it = fingers_.find(id);
  if (it == fingers_.end()) return;
  Finger& f = it->second;
  f.x = x;
  f.y = y;
  if (f.role == Role::kButton) {
    const TouchButton& b = layout_.buttons[f.button];
    if (Inside(b, x, y, kHoldSlop)) return;
    // Off its button: onto another one, or nothing until it reaches one.
    if (const int other = ButtonAt(x, y); other >= 0) {
      f.button = other;
    } else {
      f.role = Role::kLostButton;
      f.button = -1;
    }
  } else if (f.role == Role::kLostButton) {
    if (const int b = ButtonAt(x, y); b >= 0) {
      f.role = Role::kButton;
      f.button = b;
    }
  }
}

void TouchPad::Up(uint64_t id) { fingers_.erase(id); }

bool TouchPad::stick_held() const {
  return std::any_of(fingers_.begin(), fingers_.end(),
                     [](const auto& kv) { return kv.second.role == Role::kStick; });
}

float TouchPad::stick_base_x() const {
  for (const auto& [id, f] : fingers_) {
    if (f.role == Role::kStick) return f.base_x;
  }
  return layout_.stick_x;
}

float TouchPad::stick_base_y() const {
  for (const auto& [id, f] : fingers_) {
    if (f.role == Role::kStick) return f.base_y;
  }
  return layout_.stick_y;
}

PadState TouchPad::State() const {
  PadState pad;
  for (const auto& [id, f] : fingers_) {
    if (f.role == Role::kButton) {
      const TouchButton& b = layout_.buttons[f.button];
      pad.buttons |= b.buttons;
      if (b.shape == TouchShape::kDpad) pad.buttons |= DpadDirections(b, f.x, f.y);
      if (b.left_trigger) pad.left_trigger = 255;
      if (b.right_trigger) pad.right_trigger = 255;
    } else if (f.role == Role::kStick && layout_.stick_radius > 0) {
      float dx = f.x - f.base_x, dy = f.y - f.base_y;
      const float len = std::hypot(dx, dy);
      if (len > layout_.stick_radius) {
        dx *= layout_.stick_radius / len;
        dy *= layout_.stick_radius / len;
      }
      // XInput's y points up; the screen's down.
      pad.left_x = static_cast<int16_t>(std::lround(dx / layout_.stick_radius * 32767));
      pad.left_y = static_cast<int16_t>(std::lround(-dy / layout_.stick_radius * 32767));
    }
  }
  return pad;
}

}  // namespace torchlight::input
