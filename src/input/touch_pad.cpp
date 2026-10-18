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
  // From the right and the bottom edge, in millimetres, to pixels (y down).
  const auto right = [&](float from_right) { return w - from_right * mm; };
  const auto bottom = [&](float from_bottom) { return h - from_bottom * mm; };

  TouchLayout layout;
  // The attack cluster: X in the corner, the four skills on an arc 40 degrees apart (their chord,
  // 13.7 mm, keeps the 1.5 mm gap between 11 mm buttons), A apart on the arc's left.
  const float attack_r = kAttack / 2, skill_r = kSkill / 2, small_r = kSmall / 2;
  const float cx_mm = 20, cy_mm = 20, arc = 20;  // X's centre from the right and the bottom
  layout.buttons.push_back({"X", pad::kX, false, false, right(cx_mm), bottom(cy_mm), attack_r * mm});
  struct Skill {
    std::string_view name;
    uint16_t bits;
    bool lt, rt;
    float degrees;
  };
  for (const Skill& s : {Skill{"LT", 0, true, false, 185}, Skill{"Y", pad::kY, false, false, 145},
                         Skill{"B", pad::kB, false, false, 105}, Skill{"RT", 0, false, true, 65}}) {
    const float a = s.degrees * std::numbers::pi_v<float> / 180;
    layout.buttons.push_back({s.name, s.bits, s.lt, s.rt, right(cx_mm - arc * std::cos(a)),
                              bottom(cy_mm + arc * std::sin(a)), skill_r * mm});
  }
  layout.buttons.push_back({"A", pad::kA, false, false, right(cx_mm + 36), bottom(cy_mm - 2), skill_r * mm});
  // The stick's resting place and the potions above it.
  const float stick_x_mm = 25, stick_y_mm = 25;
  layout.stick_x = stick_x_mm * mm;
  layout.stick_y = bottom(stick_y_mm);
  layout.stick_radius = kStickRadius * mm;
  layout.buttons.push_back({"LB", pad::kLeftShoulder, false, false, 16 * mm, bottom(44), small_r * mm});
  layout.buttons.push_back({"RB", pad::kRightShoulder, false, false, 30 * mm, bottom(44), small_r * mm});
  // Back and Start along the top, 44 mm in from the sides: in the corners they met the potions
  // and the skills' arc on a short screen (the POCO's landscape is about 60 mm tall), and there
  // the HUD's orbs are.
  layout.buttons.push_back({"Back", pad::kBack, false, false, 44 * mm, (kLongMargin + small_r) * mm, small_r * mm});
  layout.buttons.push_back({"Start", pad::kStart, false, false, right(44), (kLongMargin + small_r) * mm,
                            small_r * mm});
  // The stick's zone: the lower left 40% x 60% of the screen.
  layout.zone_x0 = 0;
  layout.zone_y0 = h * 0.4f;
  layout.zone_x1 = w * 0.4f;
  layout.zone_y1 = h;
  return layout;
}

void TouchPad::SetLayout(TouchLayout layout) {
  layout_ = std::move(layout);
  fingers_.clear();
}

int TouchPad::ButtonAt(float x, float y) const {
  int best = -1;
  float best_d = 0;
  for (int i = 0; i < static_cast<int>(layout_.buttons.size()); ++i) {
    const TouchButton& b = layout_.buttons[i];
    const float d = Distance(x, y, b.x, b.y);
    if (d <= b.radius && (best < 0 || d < best_d)) {
      best = i;
      best_d = d;
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
    if (Distance(x, y, b.x, b.y) <= b.radius * kHoldSlop) return;
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
