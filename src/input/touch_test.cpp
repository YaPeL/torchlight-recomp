// Tests for the touch controls' pure parts (docs/android-port.md, A4.1): the gameplay layout on the
// two test devices, the finger rules (held buttons, sliding, the stick, several fingers at once),
// Auto / Always / Never, and the keystroke queue the touch driver shares with the input scripts.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "input/pad_state.h"
#include "input/touch_pad.h"
#include "input/touch_visibility.h"

namespace {

using namespace torchlight::input;

void Check(bool ok, const std::string& what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    std::exit(1);
  }
}

// Android's density buckets (160 dpi = 1.0), as SDL reports them: the POCO X7 5G (2712x1220,
// about 446 ppi) and the Galaxy Tab S5e (2560x1600, about 287 ppi).
constexpr float kMmPerInch = 25.4f;
TouchScreen Poco() { return {2712, 1220, 2.75f * 160 / kMmPerInch}; }
// What the POCO reported on the device (2026-10-18): 20.5 px/mm, so about 59.5 mm tall.
TouchScreen PocoMeasured() { return {2712, 1220, 20.5f}; }
// A 16:9-ish 6.1" phone at the 3.0 bucket: 2400x1080, about 57 mm tall (kMinHeightMm).
TouchScreen ShortPhone() { return {2400, 1080, 3.0f * 160 / kMmPerInch * (1080.0f / 1080.0f)}; }
TouchScreen Tab() { return {2560, 1600, 1.8f * 160 / kMmPerInch}; }

const TouchButton& Find(const TouchLayout& layout, std::string_view name) {
  for (const auto& b : layout.buttons) {
    if (b.name == name) return b;
  }
  std::fprintf(stderr, "FAIL: no button %.*s\n", static_cast<int>(name.size()), name.data());
  std::exit(1);
}

void TestLayout(const TouchScreen& screen, const char* device) {
  const TouchLayout layout = GameplayLayout(screen);
  const std::string d = device;
  const float mm = screen.px_per_mm;
  const float slack = 0.01f * mm;  // float rounding
  Check(layout.buttons.size() == 10, d + ": X, four skills, A, LB, RB, Back, Start");
  for (const auto& b : layout.buttons) {
    const std::string n = d + " " + std::string(b.name);
    Check(b.radius * 2 >= touch_mm::kSmall * mm - slack, n + ": at least 9 mm (48 dp)");
    Check(b.x - b.radius >= touch_mm::kSideMargin * mm - slack &&
              b.x + b.radius <= screen.width - touch_mm::kSideMargin * mm + slack,
          n + ": inside the side margins");
    Check(b.y - b.radius >= touch_mm::kLongMargin * mm - slack &&
              b.y + b.radius <= screen.height - touch_mm::kLongMargin * mm + slack,
          n + ": inside the long edges' margins");
    for (const auto& o : layout.buttons) {
      if (&o == &b) continue;
      const float gap = std::hypot(o.x - b.x, o.y - b.y) - b.radius - o.radius;
      Check(gap >= touch_mm::kMinGap * mm - slack, n + " and " + std::string(o.name) + ": 1.5 mm apart");
    }
    // The stick's resting base stays clear of the buttons.
    const float stick_gap = std::hypot(layout.stick_x - b.x, layout.stick_y - b.y) - b.radius - layout.stick_radius;
    Check(stick_gap >= 0, n + ": clear of the stick's base");
  }
  Check(Find(layout, "X").radius > Find(layout, "Y").radius, d + ": attack is the largest");
  Check(layout.stick_x - layout.stick_radius >= touch_mm::kSideMargin * mm - slack &&
            layout.stick_y + layout.stick_radius <= screen.height - touch_mm::kLongMargin * mm + slack,
        d + ": the stick inside the margins");
}

void TestHeldButton() {
  TouchPad pad(GameplayLayout(Poco()));
  const TouchButton x = Find(pad.layout(), "X");
  pad.Down(1, x.x, x.y);
  Check(pad.State().buttons == pad::kX, "X pressed when the finger lands");
  for (int i = 0; i < 100; ++i) pad.Move(1, x.x + 1, x.y);
  Check(pad.State().buttons == pad::kX, "X held while the finger stays");
  pad.Move(1, x.x + x.radius * 1.2f, x.y);
  Check(pad.State().buttons == pad::kX, "a thumb rolling a little keeps it");
  pad.Move(1, x.x + x.radius * 1.3f, x.y);
  Check(pad.State().buttons == 0, "released when the finger leaves it");
  pad.Move(1, x.x, x.y);
  Check(pad.State().buttons == pad::kX, "pressed again when the same finger comes back");
  Check(pad.tracks(1), "the finger is tracked");
  pad.Up(1);
  Check(pad.State() == PadState{} && !pad.tracks(1), "released when the finger lifts");
  pad.Down(2, x.x, x.y);
  pad.SetLayout(GameplayLayout(Tab()));
  Check(!pad.tracks(2) && pad.State() == PadState{}, "a new layout drops the fingers");
}

void TestSliding() {
  TouchPad pad(GameplayLayout(Poco()));
  const TouchButton y = Find(pad.layout(), "Y"), b = Find(pad.layout(), "B");
  pad.Down(1, y.x, y.y);
  pad.Move(1, b.x, b.y);
  Check(pad.State().buttons == pad::kB, "sliding from Y onto B presses B and lets Y go");
  // A finger that landed between the controls never presses one.
  const float mid_x = (y.x + b.x) / 2, mid_y = (y.y + b.y) / 2 - b.radius * 3;
  pad.Down(2, mid_x, mid_y);
  pad.Move(2, y.x, y.y);
  Check(pad.State().buttons == pad::kB, "a finger landed on nothing does not press Y");
  const TouchButton rt = Find(pad.layout(), "RT");
  pad.Down(3, rt.x, rt.y);
  Check(pad.State().right_trigger == 255, "RT is the right trigger, pulled");
}

void TestStick() {
  const TouchScreen screen = Poco();
  TouchPad pad(GameplayLayout(screen));
  const float r = pad.layout().stick_radius;
  // Floating: the base goes where the thumb lands.
  const float x0 = screen.width * 0.2f, y0 = screen.height * 0.8f;
  pad.Down(1, x0, y0);
  Check(pad.stick_held() && pad.stick_base_x() == x0 && pad.stick_base_y() == y0, "the stick under the thumb");
  Check(pad.State() == PadState{}, "no movement before the thumb moves");
  pad.Move(1, x0 + r, y0);
  Check(pad.State().left_x == 32767 && pad.State().left_y == 0, "right at full");
  pad.Move(1, x0, y0 - r * 3);
  Check(pad.State().left_x == 0 && pad.State().left_y == 32767, "up at full, clamped to the radius");
  pad.Move(1, screen.width * 0.9f, y0);
  Check(pad.State().left_x == 32767, "the stick stays the finger's wherever it goes");
  // A second finger in the zone does not take it.
  pad.Down(2, x0 + 10, y0 + 10);
  Check(pad.stick_base_x() == x0, "one stick, one finger");
  pad.Up(1);
  Check(pad.State() == PadState{} && !pad.stick_held(), "lifted: centred");
  Check(pad.stick_base_x() == pad.layout().stick_x, "back at its resting place");
  // Outside the zone (upper half, off the buttons): nothing.
  pad.Down(3, screen.width * 0.5f, screen.height * 0.2f);
  Check(!pad.stick_held() && pad.State() == PadState{}, "outside the zone, nothing");
}

void TestSeveralFingers() {
  // The owner's case: move with the stick, hold X, tap LB, then lift in another order.
  for (const TouchScreen& screen : {Poco(), Tab()}) {
    TouchPad pad(GameplayLayout(screen));
    const float r = pad.layout().stick_radius;
    const TouchButton x = Find(pad.layout(), "X"), lb = Find(pad.layout(), "LB");
    const float sx = pad.layout().stick_x, sy = pad.layout().stick_y;
    pad.Down(10, sx, sy);
    pad.Move(10, sx - r, sy);
    pad.Down(11, x.x, x.y);
    PadState s = pad.State();
    Check(s.left_x == -32767 && s.buttons == pad::kX, "moving left while attacking");
    pad.Down(12, lb.x, lb.y);
    s = pad.State();
    Check(s.left_x == -32767 && s.buttons == (pad::kX | pad::kLeftShoulder), "and drinking a potion");
    pad.Up(11);
    s = pad.State();
    Check(s.left_x == -32767 && s.buttons == pad::kLeftShoulder, "the attack lifted first");
    pad.Up(10);
    Check(pad.State().left_x == 0 && pad.State().buttons == pad::kLeftShoulder, "then the stick");
    pad.Up(12);
    Check(pad.State() == PadState{}, "all lifted");
  }
}

void TestVisibility() {
  {
    TouchVisibility v(TouchMode::kAuto, /*pad_at_start=*/false, /*touch=*/true);
    Check(v.visible(), "auto, no pad: shown");
    Check(v.OnFingerDown(), "a finger on shown controls acts");
    Check(v.OnPadUsed() && !v.visible(), "a pad used: hidden");
    Check(!v.OnPadUsed(), "already hidden");
    Check(!v.OnFingerDown() && v.visible(), "the next touch shows them, and only shows them");
    Check(v.OnFingerDown(), "the touch after acts");
  }
  {
    TouchVisibility v(TouchMode::kAuto, /*pad_at_start=*/true, /*touch=*/true);
    Check(!v.visible(), "auto, a pad at start: hidden");
    Check(!v.OnFingerDown() && v.visible(), "a touch shows them with the pad still connected");
  }
  {
    TouchVisibility v(TouchMode::kAuto, /*pad_at_start=*/false, /*touch=*/false);
    Check(!v.visible() && !v.OnFingerDown() && !v.visible(), "no touch screen: never shown");
  }
  {
    TouchVisibility v(TouchMode::kAlways, /*pad_at_start=*/true, /*touch=*/true);
    Check(v.visible() && !v.OnPadUsed() && v.visible() && v.OnFingerDown(), "always: shown");
  }
  {
    TouchVisibility v(TouchMode::kNever, /*pad_at_start=*/false, /*touch=*/true);
    Check(!v.visible() && !v.OnFingerDown() && !v.visible(), "never: a touch does not bring them");
  }
}

void TestKeystrokes() {
  using Clock = KeystrokeQueue::Clock;
  KeystrokeQueue q;
  const auto t0 = Clock::now();
  PadState up, a;
  a.buttons = pad::kA;
  q.Push(up, a, t0);
  q.Push(a, up, t0);
  auto k = q.Pop(t0);
  Check(k && k->virtual_key == 0x5800 && k->down, "A down");
  k = q.Pop(t0);
  Check(k && k->virtual_key == 0x5800 && !k->down, "then A up");
  Check(!q.Pop(t0), "then nothing");
  q.Push(up, a, t0);
  Check(!q.Pop(t0 + KeystrokeQueue::kLifetime + std::chrono::milliseconds(1)), "unread for too long: dropped");
  PadState lt;
  lt.left_trigger = 255;
  q.Push(up, lt, t0);
  k = q.Pop(t0);
  Check(k && k->virtual_key == 0x5806 && k->down, "a trigger pulled is its key");
}

}  // namespace

int main() {
  TestLayout(Poco(), "POCO X7");
  TestLayout(PocoMeasured(), "POCO X7 as measured");
  Check(1080 / ShortPhone().px_per_mm >= kMinHeightMm, "the short phone is the shortest supported");
  TestLayout(ShortPhone(), "a short phone");
  TestLayout(Tab(), "Tab S5e");
  TestHeldButton();
  TestSliding();
  TestStick();
  TestSeveralFingers();
  TestVisibility();
  TestKeystrokes();
  std::puts("touch_test: ok");
  return 0;
}
