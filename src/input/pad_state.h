// The controller state the game reads, shared by the host's own controllers (the touch controls,
// live/touch_input.h; the development input scripts, dev/script_input.h): the XINPUT_GAMEPAD
// buttons and axes, and the keystrokes a change of buttons gives the menus that read keys.

#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

#include <rex/input/keystroke_repeat.h>

namespace torchlight::input {

// XINPUT_GAMEPAD button bits.
namespace pad {
constexpr uint16_t kUp = 0x0001, kDown = 0x0002, kLeft = 0x0004, kRight = 0x0008;
constexpr uint16_t kStart = 0x0010, kBack = 0x0020, kLeftThumb = 0x0040, kRightThumb = 0x0080;
constexpr uint16_t kLeftShoulder = 0x0100, kRightShoulder = 0x0200;
constexpr uint16_t kA = 0x1000, kB = 0x2000, kX = 0x4000, kY = 0x8000;
}  // namespace pad

struct PadState {
  uint16_t buttons = 0;
  uint8_t left_trigger = 0, right_trigger = 0;
  int16_t left_x = 0, left_y = 0, right_x = 0, right_y = 0;
  bool operator==(const PadState&) const = default;
};

// XINPUT keystrokes (VK_PAD_*) for the buttons that changed between two states: what
// XamInputGetKeystroke reports to menus that read keys rather than the state.
struct PadKeystroke {
  uint16_t virtual_key;
  bool down;
  bool repeat = false;  // a held key's repeated down
};
std::vector<PadKeystroke> PadKeystrokes(const PadState& before, const PadState& after);

// The pad's state as XInput's keystroke bits, as the SDK's SDL pad driver builds them
// (sdl_input_driver.cpp, AnalogToKeyfield and its key table), so a host controller's keystrokes
// match a real pad's: bits 0-15 the buttons; 16-17 the triggers past kTriggerKeyThreshold; 18-25
// the left stick's directions past kThumbKeyThreshold (up, down, right, left, then the diagonals,
// which replace their two sides); 26-33 the right stick's the same. The menus that read keystrokes
// move with the stick through these (VK_PAD_LTHUMB_*).
constexpr int kPadKeyCount = 34;
constexpr int16_t kThumbKeyThreshold = 0x4E00;
constexpr uint8_t kTriggerKeyThreshold = 0x1F;
uint64_t PadKeyBits(const PadState& pad);
// The virtual key (VK_PAD_*) of a keystroke bit; 0 for the bits that have none.
uint16_t PadKeyVirtualKey(int bit);

// A host controller's keystrokes as a real pad's: rex's NextKeystroke (the SDK's SDL pad driver's,
// with its repeat) over PadKeyBits of the state now. A button or trigger pressed and released
// between two of the game's reads still gives its down, then its up, if the game reads within
// KeystrokeQueue::kLifetime of the press (a screen that reads no keystrokes, the title, must not
// leave presses to the next one). The sticks' directions are not kept that way: a direction the
// stick only passed through is not a step (a real pad would not report it either).
class PadKeystrokeSource {
 public:
  static constexpr uint32_t kRepeatDelayMs = 400, kRepeatRateMs = 100;  // the SDK's HID_SDL_REPEAT_*

  // The state changed at `now_ms`.
  void Changed(const PadState& next, uint32_t now_ms);
  // The next keystroke at `now_ms`; with the input inactive (a dialog has it), everything is
  // released.
  std::optional<PadKeystroke> Next(bool active, uint32_t now_ms);
  void Reset() { *this = {}; }

 private:
  static constexpr uint64_t kLatchable = (uint64_t(1) << 18) - 1;  // buttons and triggers
  PadState state_;
  rex::input::KeystrokeTracker tracker_;
  uint64_t latched_ = 0;
  uint32_t latched_at_[18] = {};
};

// The keystrokes of a host controller, waiting for the game to read them. One the game did not
// read soon after it happened is dropped: a screen that does not read keystrokes (the title) left
// its presses to the next one, which acted on them later. Not thread-safe: its owner locks.
class KeystrokeQueue {
 public:
  using Clock = std::chrono::steady_clock;
  static constexpr auto kLifetime = std::chrono::milliseconds(200);
  static constexpr size_t kMaxQueued = 64;

  // The keystrokes from `before` to `after`, stamped `now`.
  void Push(const PadState& before, const PadState& after, Clock::time_point now);
  // The oldest keystroke still alive at `now`, removed; none when there is none.
  std::optional<PadKeystroke> Pop(Clock::time_point now);
  void Clear() { keys_.clear(); }

 private:
  struct Timed {
    PadKeystroke key;
    Clock::time_point when;
  };
  std::deque<Timed> keys_;
};

}  // namespace torchlight::input
