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
//                                            (at most 10 s), the controller still, and fails when
//                                            it cannot be written
//   touch ID X Y                             a finger (ID 1-9) on the touch controls, down at X, Y
//                                            (window fractions 0 to 1, Y down) or moved there when
//                                            it is down already (docs/android-port.md, A4)
//   touch ID CONTROL                         the same at the centre of a touch control: X, Y, A,
//                                            B, LT, RT, LB, RB, Back, Start, or the d-pad's Up,
//                                            Down, Left, Right
//   release ID                               that finger lifted
//   replay FILE [trace FILE]                 a recorded controller (pad_replay.h) played by time
//                                            from this command's start: its start_ms at rest,
//                                            then each sample at its t_ms, the last one held
//                                            until kTraceTailMs after it; with "trace", the
//                                            hero's trace of every frame from t=0 to the end.
//                                            Relative paths are the script's folder's
//   command TEXT                             a guest developer command (dev/guest_command.h, e.g.
//                                            ASCEND to the town), in development builds with
//                                            TORCHLIGHT_DEV_COMMANDS too
//   quit                                     the game exits as when its window is closed
//
// Events count from the moment the command starts: a menu that opened before it does not satisfy
// it. A timeout ends the script with a failure.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "dev/pad_replay.h"
#include "input/pad_state.h"

namespace torchlight::dev {

// The controller state the game reads (input/pad_state.h, shared with the touch controls).
namespace pad = input::pad;
using input::PadState;

// What the game did, as the app sees it; counters only grow.
struct ScriptEvents {
  uint64_t frame = 0;       // guest frames (swaps) since the start
  double seconds = 0;       // host seconds since the start
  uint32_t level_loads = 0; // level loads completed
  std::vector<std::string> menus_opened;  // class names of the menus opened, in order
  // The game UI as last read (script_input.cpp), when a script waits on it: how many times it was
  // read, the windows the open menus' pad focus is on and the windows shown.
  uint64_t ui_reads = 0;
  uint64_t dumps_written = 0;  // dump_ui files written
  uint64_t dumps_failed = 0;   // dump_ui files that could not be written (no capture folder...)
  std::vector<std::string> focused;
  std::vector<std::string> shown_windows;
};

struct ScriptAction {
  enum class Kind { kMark, kCapture, kCommand, kDumpUi, kQuit, kTouch, kFailed, kFinished };
  Kind kind;
  // kMark: the step's name; kCapture: its name (may be empty); kCommand: the command; kDumpUi: the
  // file's name; kFailed: why
  std::string text;
  // kTouch: the finger, where (window fractions, or the control named in `text`) and whether it
  // lifts.
  int finger = 0;
  float x = 0, y = 0;
  bool release = false;
};

// Frames the controller stays still after a capture, so the next command's press does not reach
// the captured frame.
inline constexpr uint64_t kCaptureFrames = 4;

// A file's text by the name a script gives it (replay's recording); nullopt when it cannot be read.
using ScriptFileReader = std::function<std::optional<std::string>(std::string_view name)>;

// A frame of a replay: its time since t=0, the controller given to the game and the trace's file
// (empty: none).
struct ReplayFrame {
  int64_t t_us = 0;
  PadState pad;
  std::string trace;
  size_t command = 0;  // the replay command's index in the script: each one writes its own trace
};

class InputScript {
 public:
  // Nullopt with `error` ("line N: ...") when the text is not a script. `read` gives the files
  // the script names (replay); without it a replay is an error.
  static std::optional<InputScript> Parse(std::string_view text, std::string& error,
                                          const ScriptFileReader& read = {});

  // Once per guest frame: runs the commands that complete now, appends their actions (and
  // kFinished or kFailed once at the end), and returns the controller state for this frame.
  PadState Step(const ScriptEvents& events, std::vector<ScriptAction>& actions);

  // Set by the last Step when it played a replay's frame (from t=0 to the end of its trace).
  const std::optional<ReplayFrame>& replay_frame() const { return replay_frame_; }

  bool done() const { return next_ >= commands_.size(); }
  size_t size() const { return commands_.size(); }
  // Whether any command waits on the game UI (focus, window): the app reads it only then.
  bool ReadsUi() const;

 private:
  enum class Op { kPress, kPressUntil, kPressFor, kHold, kStick, kIdle, kWait, kMark, kCapture, kCommand, kDumpUi, kQuit, kTouch, kRelease, kReplay };
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
    int finger = 0;    // touch, release
    float x = 0, y = 0;
    std::shared_ptr<const PadReplay> replay;  // kReplay
    std::string trace;                        // kReplay: the trace's file, empty for none
  };
  struct Start {
    uint64_t frame = 0;
    double seconds = 0;
    uint32_t level_loads = 0;
    size_t menus = 0;
    uint64_t ui_reads = 0;
    uint64_t dumps = 0;
    uint64_t dumps_failed = 0;
  };

  static std::string Describe(const Command& c);
  bool Elapsed(const Duration& d, const ScriptEvents& e, const Start& since) const;
  bool EventSeen(const Command& c, const ScriptEvents& e) const;
  void Begin(const ScriptEvents& e);

  std::vector<Command> commands_;
  size_t next_ = 0;
  bool started_ = false;  // commands_[next_] has begun
  Start start_;           // when it began
  double replay_prev_ms_ = 0;  // kReplay: the previous frame's time (Between)
  Start press_;           // kPressUntil: when the current press began
  bool ended_ = false;    // kFinished or kFailed was reported
  std::optional<ReplayFrame> replay_frame_;
};

using input::PadKeystroke;
using input::PadKeystrokes;

}  // namespace torchlight::dev
