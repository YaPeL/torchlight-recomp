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
// A 20:9 5.5" phone at the 3.0 bucket: 2400x1080, about 57 x 127 mm (kMinHeightMm).
TouchScreen ShortPhone() { return {2400, 1080, 3.0f * 160 / kMmPerInch}; }
// The narrowest realistic case: 16:9 at 57 mm tall, about 101 mm wide (kMinWidthMm).
TouchScreen Narrow169() { return {1920, 1080, 3.0f * 160 / kMmPerInch}; }
TouchScreen Tab() { return {2560, 1600, 1.8f * 160 / kMmPerInch}; }

const TouchButton& Find(const TouchLayout& layout, std::string_view name) {
  for (const auto& b : layout.buttons) {
    if (b.name == name) return b;
  }
  std::fprintf(stderr, "FAIL: no button %.*s\n", static_cast<int>(name.size()), name.data());
  std::exit(1);
}

// A control's box (wide ones) or circle, for the margins and the gaps.
float HalfW(const TouchButton& b) { return b.shape == TouchShape::kWide ? b.half_width : b.radius; }
float Gap(const TouchButton& a, const TouchButton& b) {
  if (a.shape != TouchShape::kWide && b.shape != TouchShape::kWide) {
    return std::hypot(a.x - b.x, a.y - b.y) - a.radius - b.radius;
  }
  const float gx = std::abs(a.x - b.x) - HalfW(a) - HalfW(b), gy = std::abs(a.y - b.y) - a.radius - b.radius;
  return std::max(gx, gy);
}

void TestLayout(const TouchScreen& screen, const char* device) {
  const TouchLayout layout = GameplayLayout(screen);
  const std::string d = device;
  const float mm = screen.px_per_mm;
  const float slack = 0.01f * mm;  // float rounding
  Check(layout.buttons.size() == 14, d + ": A, B, X, Y, the d-pad, four shoulders, Back, Start");
  for (const auto& b : layout.buttons) {
    const std::string n = d + " " + std::string(b.name);
    Check(b.radius * 2 >= touch_mm::kSmall * mm - slack, n + ": at least 9 mm (48 dp)");
    Check(b.x - HalfW(b) >= touch_mm::kSideMargin * mm - slack &&
              b.x + HalfW(b) <= screen.width - touch_mm::kSideMargin * mm + slack,
          n + ": inside the side margins");
    Check(b.y - b.radius >= touch_mm::kLongMargin * mm - slack &&
              b.y + b.radius <= screen.height - touch_mm::kLongMargin * mm + slack,
          n + ": inside the long edges' margins");
    for (const auto& o : layout.buttons) {
      if (&o == &b) continue;
      Check(Gap(b, o) >= touch_mm::kMinGap * mm - slack, n + " and " + std::string(o.name) + ": 1.5 mm apart");
    }
    // The stick's resting base stays clear of the buttons.
    TouchButton stick;
    stick.x = layout.stick_x;
    stick.y = layout.stick_y;
    stick.radius = layout.stick_radius;
    Check(Gap(stick, b) >= 0, n + ": clear of the stick's base");
  }
  Check(layout.stick_x - layout.stick_radius >= touch_mm::kSideMargin * mm - slack &&
            layout.stick_y + layout.stick_radius <= screen.height - touch_mm::kLongMargin * mm + slack,
        d + ": the stick inside the margins");
  // The pad's diamond: Y above A, X left of B.
  Check(Find(layout, "Y").y < Find(layout, "A").y && Find(layout, "X").x < Find(layout, "B").x, d + ": the diamond");
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
  // A finger that landed between the controls (between Y and B, outside both) never presses one.
  pad.Down(2, (y.x + b.x) / 2, (y.y + b.y) / 2);
  pad.Move(2, y.x, y.y);
  Check(pad.State().buttons == pad::kB, "a finger landed on nothing does not press Y");
  const TouchButton rt = Find(pad.layout(), "RT");
  pad.Down(3, rt.x, rt.y);
  Check(pad.State().right_trigger == 255, "RT is the right trigger, pulled");
  // A wide control is hit across its width, not only within its height's circle.
  pad.Up(3);
  pad.Down(4, rt.x + rt.half_width * 0.9f, rt.y);
  Check(pad.State().right_trigger == 255, "RT is hit near its end");
  const TouchButton up = Find(pad.layout(), "Up");
  pad.Down(5, up.x, up.y);
  Check((pad.State().buttons & pad::kUp) != 0, "the d-pad's up");
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
  pad.Down(3, screen.width * 0.6f, screen.height * 0.25f);
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

void TestKeyBits() {
  // As the SDK's SDL pad driver: past 0x4E00 a stick is a direction, both axes a diagonal.
  PadState p;
  p.left_y = kThumbKeyThreshold;
  Check(PadKeyBits(p) == 0, "at the threshold, nothing");
  p.left_y = kThumbKeyThreshold + 1;
  Check(PadKeyBits(p) == (uint64_t(1) << 18) && PadKeyVirtualKey(18) == 0x5820, "left stick up: VK_PAD_LTHUMB_UP");
  p.left_y = -32768;
  Check(PadKeyBits(p) == (uint64_t(1) << 19) && PadKeyVirtualKey(19) == 0x5821, "down");
  p.left_x = 32767;
  Check(PadKeyBits(p) == (uint64_t(1) << 24) && PadKeyVirtualKey(24) == 0x5826, "down and right: the diagonal only");
  p = {};
  p.right_x = -32768;
  Check(PadKeyBits(p) == (uint64_t(1) << 29) && PadKeyVirtualKey(29) == 0x5833, "right stick left");
  p = {};
  p.left_trigger = kTriggerKeyThreshold + 1;
  p.buttons = pad::kA | pad::kLeftShoulder;
  Check(PadKeyBits(p) == ((uint64_t(1) << 16) | pad::kA | pad::kLeftShoulder), "a trigger and buttons");
  Check(PadKeyVirtualKey(12) == 0x5800 && PadKeyVirtualKey(8) == 0x5805 && PadKeyVirtualKey(9) == 0x5804 &&
            PadKeyVirtualKey(16) == 0x5806 && PadKeyVirtualKey(0) == 0x5810,
        "A, LB, RB, LT and d-pad up keys");
  Check(PadKeyVirtualKey(10) == 0 && PadKeyVirtualKey(11) == 0 && PadKeyVirtualKey(34) == 0, "bits with no key");
}

void TestKeystrokeSource() {
  PadKeystrokeSource src;
  PadState up, a;
  a.buttons = pad::kA;
  // A tap between two reads: its down, then its up.
  src.Changed(a, 1000);
  src.Changed(up, 1030);
  auto k = src.Next(true, 1040);
  Check(k && k->virtual_key == 0x5800 && k->down && !k->repeat, "a short tap: down");
  k = src.Next(true, 1041);
  Check(k && k->virtual_key == 0x5800 && !k->down, "then up");
  Check(!src.Next(true, 1042), "then nothing");
  // A tap the game reads too late (a screen that reads no keystrokes): dropped.
  src.Changed(a, 2000);
  src.Changed(up, 2030);
  Check(!src.Next(true, 2000 + 201), "a tap older than the lifetime is dropped");
  // A held stick direction repeats as a pad's: the down, then after 400 ms every 100 ms.
  PadState down;
  down.left_y = -32768;
  src.Changed(down, 3000);
  k = src.Next(true, 3001);
  Check(k && k->virtual_key == 0x5821 && k->down && !k->repeat, "stick down: VK_PAD_LTHUMB_DOWN");
  Check(!src.Next(true, 3300), "no repeat before the delay");
  k = src.Next(true, 3402);
  Check(k && k->repeat, "a repeat after 400 ms");
  Check(!src.Next(true, 3450), "not before the rate");
  k = src.Next(true, 3503);
  Check(k && k->repeat, "then every 100 ms");
  src.Changed(up, 3510);
  k = src.Next(true, 3511);
  Check(k && k->virtual_key == 0x5821 && !k->down, "released: up");
  // A direction the stick only passed through between two reads is not a step.
  PadState right;
  right.left_x = 32767;
  src.Changed(down, 4000);
  src.Changed(right, 4010);
  k = src.Next(true, 4020);
  Check(k && k->virtual_key == 0x5822 && k->down, "only the direction it is in now: right");
  Check(!src.Next(true, 4021), "and no down for the one it passed");
  src.Changed(up, 4030);
  Check(src.Next(true, 4031).has_value(), "right up");
  // Inactive (a dialog has the input): released, and a tap made then is not kept.
  src.Changed(a, 5000);
  k = src.Next(false, 5001);
  Check(!k, "inactive: nothing down");
  src.Changed(up, 5002);
  Check(!src.Next(true, 5003), "the tap made while inactive is not delivered later");
}

}  // namespace

int main() {
  TestLayout(Poco(), "POCO X7");
  TestLayout(PocoMeasured(), "POCO X7 as measured");
  Check(1080 / ShortPhone().px_per_mm >= kMinHeightMm, "the short phone is the shortest supported");
  TestLayout(ShortPhone(), "a short phone");
  TestLayout(Narrow169(), "a 16:9 phone at the minimum height");
  TestKeyBits();
  TestKeystrokeSource();
  Check(1920 / Narrow169().px_per_mm >= kMinWidthMm, "the 16:9 phone is the narrowest supported");
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
