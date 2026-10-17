// Development only (TORCHLIGHT_DEV_INPUT): a script of controller input played into the game, so
// recording, profiling and validation runs need no one at the controller (docs/dev-input-script.md).
// This part is pure: the script's text, and the state machine that, once per guest frame, gives the
// controller state for that frame and the actions to run. The input driver and the game events that
// feed it are in the app (script_input.cpp).
//
// One command per line; '#' starts a comment. Durations are N, Ns, Nms or Nf (seconds by default;
// f counts guest frames). Buttons: A B X Y START BACK LB RB LT RT UP DOWN LEFT RIGHT LS RS.
//
//   press BUTTON... [frames N]               down N frames (default 6), then up as long
//   press BUTTON... every DUR until EVENT [timeout DUR]
//                                            press again every DUR until EVENT happens
//   press BUTTON... every DUR for DUR        press again every DUR, for that long
//   hold BUTTON... for DUR
//   stick left|right X Y for DUR             X, Y from -1 to 1 (Y up is positive)
//   idle DUR
//   wait EVENT [timeout DUR]                 default timeout 120 s; EVENT is one of
//                                              level_loaded
//                                              menu NAME    a menu whose class name contains NAME
//                                                           opens
//                                              focus NAME   the pad focus of an open menu is on the
//                                                           window named NAME, or whose text (or
//                                                           a child's: a button's label) is NAME
//                                              window NAME  a window named NAME is shown (an inner
//                                                           screen: a layout's root, a page, a tab)
//                                            Window names match whole, ignoring case, with or
//                                            without the layout's instance prefix ("43_Help"
//                                            matches Help).
//   focus NAME [by UP|DOWN|LEFT|RIGHT] [every DUR] [timeout DUR]
//                                            press the direction (DOWN) every DUR (0.4 s) until
//                                            the focus is on NAME (timeout 30 s)
//   mark TEXT                                a step of the run, in the log as the step overlay's
//   capture [NAME]                           an F9 capture of the next frame, NAME in the log; the
//                                            controller stays still for kCaptureFrames
//   dump_ui NAME                             the game UI's window tree, as shown now, to
//                                            <capture folder>/NAME.json; the script waits for it
//                                            (at most 10 s), the controller still
//   command TEXT                             a guest developer command (dev/guest_command.h, e.g.
//                                            ASCEND to the town), in development builds with
//                                            TORCHLIGHT_DEV_COMMANDS too
//   quit                                     the game exits as when its window is closed
//
// Events count from the moment the command starts: a menu that opened before it does not satisfy
// it. A timeout ends the script with a failure.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace torchlight::dev {

// XINPUT_GAMEPAD button bits and the controller state the game reads.
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

// What the game did, as the app sees it; counters only grow.
struct ScriptEvents {
  uint64_t frame = 0;       // guest frames (swaps) since the start
  double seconds = 0;       // host seconds since the start
  uint32_t level_loads = 0; // level loads completed
  std::vector<std::string> menus_opened;  // class names of the menus opened, in order
  // The game UI as last read (dev/ui_probe.h), when a script waits on it: how many times it was
  // read, the windows the open menus' pad focus is on and the windows shown.
  uint64_t ui_reads = 0;
  uint64_t dumps_written = 0;  // dump_ui files written (or given up on)
  std::vector<std::string> focused;
  std::vector<std::string> shown_windows;
};

struct ScriptAction {
  enum class Kind { kMark, kCapture, kCommand, kDumpUi, kQuit, kFailed, kFinished };
  Kind kind;
  // kMark: the step's name; kCapture: its name (may be empty); kCommand: the command; kDumpUi: the
  // file's name; kFailed: why
  std::string text;
};

// Frames the controller stays still after a capture, so the next command's press does not reach
// the captured frame.
inline constexpr uint64_t kCaptureFrames = 4;

class InputScript {
 public:
  // Nullopt with `error` ("line N: ...") when the text is not a script.
  static std::optional<InputScript> Parse(std::string_view text, std::string& error);

  // Once per guest frame: runs the commands that complete now, appends their actions (and
  // kFinished or kFailed once at the end), and returns the controller state for this frame.
  PadState Step(const ScriptEvents& events, std::vector<ScriptAction>& actions);

  bool done() const { return next_ >= commands_.size(); }
  size_t size() const { return commands_.size(); }
  // Whether any command waits on the game UI (focus, window): the app reads it only then.
  bool ReadsUi() const;

 private:
  enum class Op { kPress, kPressUntil, kPressFor, kHold, kStick, kIdle, kWait, kMark, kCapture, kCommand, kDumpUi, kQuit };
  enum class Event { kNone, kLevelLoaded, kMenu, kFocus, kWindow };
  struct Duration {
    double value = 0;
    bool frames = false;
  };
  struct Command {
    int line = 0;
    Op op = Op::kIdle;
    PadState pad;
    uint32_t press_frames = 6;
    Duration duration, every, timeout{120, false};
    Event event = Event::kNone;
    std::string text;  // mark text, menu name
  };
  struct Start {
    uint64_t frame = 0;
    double seconds = 0;
    uint32_t level_loads = 0;
    size_t menus = 0;
    uint64_t ui_reads = 0;
    uint64_t dumps = 0;
  };

  static std::string Describe(const Command& c);
  bool Elapsed(const Duration& d, const ScriptEvents& e, const Start& since) const;
  bool EventSeen(const Command& c, const ScriptEvents& e) const;
  void Begin(const ScriptEvents& e);

  std::vector<Command> commands_;
  size_t next_ = 0;
  bool started_ = false;  // commands_[next_] has begun
  Start start_;           // when it began
  Start press_;           // kPressUntil: when the current press began
  bool ended_ = false;    // kFinished or kFailed was reported
};

// XINPUT keystrokes (VK_PAD_*) for the buttons that changed between two frames: what
// XamInputGetKeystroke reports to menus that read keys rather than the state.
struct PadKeystroke {
  uint16_t virtual_key;
  bool down;
};
std::vector<PadKeystroke> PadKeystrokes(const PadState& before, const PadState& after);

}  // namespace torchlight::dev
