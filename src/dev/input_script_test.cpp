// The input script (input_script.h): parsing and its errors, each command frame by frame against
// fake game events, event waits and their timeouts, and the keystrokes of a press.

#include "dev/input_script.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

using namespace torchlight::dev;
using Kind = ScriptAction::Kind;

int failures = 0;
void Check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

InputScript MustParse(const char* text) {
  std::string error;
  auto s = InputScript::Parse(text, error);
  if (!s) {
    std::printf("FAIL: parse: %s\n", error.c_str());
    std::exit(1);
  }
  return *s;
}

// Steps one frame at a time (60 frames a second) and keeps what came out.
struct Run {
  InputScript script;
  ScriptEvents e;
  std::vector<PadState> pads;
  std::vector<ScriptAction> actions;
  explicit Run(const char* text) : script(MustParse(text)) {}
  PadState Frame() {
    PadState p = script.Step(e, actions);
    pads.push_back(p);
    ++e.frame;
    e.seconds += 1.0 / 60;
    return p;
  }
  void Frames(int n) {
    for (int i = 0; i < n; ++i) Frame();
  }
  bool Has(Kind k) const {
    for (const auto& a : actions) if (a.kind == k) return true;
    return false;
  }
};

void TestParseErrors() {
  std::string error;
  Check(!InputScript::Parse("", error) && error.find("no commands") != std::string::npos, "empty");
  Check(!InputScript::Parse("jump\n", error) && error.find("line 1") != std::string::npos,
        "unknown command with its line");
  Check(!InputScript::Parse("# c\npress Q\n", error) && error.find("line 2") != std::string::npos,
        "unknown button");
  Check(!InputScript::Parse("stick left 2 0 for 1s\n", error), "stick out of range");
  Check(!InputScript::Parse("wait something\n", error), "unknown event");
  Check(!InputScript::Parse("hold A 3s\n", error), "hold needs for");
  Check(InputScript::Parse("press A B frames 3 # comment\r\n\nidle 2f\n", error).has_value(),
        "comments, CRLF and blank lines");
}

void TestPressAndIdle() {
  Run r("press A frames 3\nidle 2f\npress START\n");
  r.Frames(21);  // 3 + 3, 2, 6 + 6 frames
  Check(r.pads[0].buttons == pad::kA && r.pads[2].buttons == pad::kA, "A down 3 frames");
  Check(r.pads[3].buttons == 0 && r.pads[5].buttons == 0, "then up 3 frames");
  Check(r.pads[6].buttons == 0 && r.pads[7].buttons == 0, "idle 2 frames");
  Check(r.pads[8].buttons == pad::kStart && r.pads[13].buttons == pad::kStart, "START 6 frames");
  Check(r.pads[14].buttons == 0, "released");
  Check(r.Has(Kind::kFinished) && r.script.done(), "finished once at the end");
}

void TestHoldStickAndTriggers() {
  Run r("hold LT RB for 3f\nstick left 0 1 for 0.05s\n");
  r.Frames(8);
  Check(r.pads[0].left_trigger == 255 && r.pads[0].buttons == pad::kRightShoulder, "hold LT RB");
  Check(r.pads[3].left_y == 32767 && r.pads[3].left_x == 0, "stick up");
  Check(r.pads[7].left_y == 0, "stick released after its seconds");
}

void TestWaitLevelLoad() {
  Run r("wait level_loaded timeout 1s\nmark DUNGEON: stand still\ncapture\ncommand ASCEND\n");
  r.e.level_loads = 4;  // loads before the command do not count
  r.Frames(10);
  Check(!r.Has(Kind::kMark), "still waiting");
  r.e.level_loads = 5;
  r.Frames(1);
  Check(r.actions.size() == 2 && r.actions[0].kind == Kind::kMark &&
            r.actions[0].text == "DUNGEON: stand still" && r.actions[1].kind == Kind::kCapture,
        "the load ends the wait; mark and capture in the same frame");
  r.Frames(int(kCaptureFrames));
  Check(r.actions.size() >= 3 && r.actions[2].kind == Kind::kCommand && r.actions[2].text == "ASCEND",
        "the command once the capture's still frames are over");
}

void TestWaitTimeout() {
  Run r("wait menu CMainMenu timeout 10f\npress A\n");
  r.e.menus_opened = {"CMainMenu"};  // opened before: does not count
  r.Frames(12);
  Check(r.Has(Kind::kFailed) && !r.Has(Kind::kFinished), "a timeout fails the script");
  Check(r.actions[0].text.find("line 1") != std::string::npos, "the failure names the line");
  Check(r.pads.back().buttons == 0, "nothing pressed after a failure");
}

void TestPressUntilMenu() {
  Run r("press A every 10f until menu MainMenu timeout 100f\nmark menu\n");
  r.Frames(25);
  Check(r.pads[0].buttons == pad::kA && r.pads[6].buttons == 0, "first press");
  Check(r.pads[10].buttons == pad::kA && r.pads[20].buttons == pad::kA, "pressed again every 10");
  r.e.menus_opened.push_back(".?AVCMainMenu@@");
  r.Frames(1);
  Check(r.Has(Kind::kMark), "the menu ends the presses");
}

void TestPressFor() {
  Run r("press X every 10f for 30f\nmark after\n");
  r.Frames(31);
  Check(r.pads[0].buttons == pad::kX && r.pads[6].buttons == 0 && r.pads[10].buttons == pad::kX &&
            r.pads[20].buttons == pad::kX && r.pads[29].buttons == 0,
        "pressed every 10 frames");
  Check(r.Has(Kind::kMark), "then the next command, after 30 frames");
}

void TestKeystrokes() {
  PadState up, down;
  down.buttons = pad::kA | pad::kDown;
  down.right_trigger = 200;
  auto keys = PadKeystrokes(up, down);
  Check(keys.size() == 3, "A, DOWN and RT pressed");
  bool a = false;
  for (auto k : keys) a |= k.virtual_key == 0x5800 && k.down;
  Check(a, "VK_PAD_A down");
  keys = PadKeystrokes(down, up);
  Check(keys.size() == 3 && !keys[0].down, "and released");
  Check(PadKeystrokes(down, down).empty(), "no change, no keys");
}


void TestUiCommandsParse() {
  std::string error;
  Check(InputScript::Parse("focus Options\n", error).has_value(), "focus NAME");
  Check(InputScript::Parse("focus Options by UP every 0.2s timeout 10s\n", error).has_value(), "focus with options");
  Check(!InputScript::Parse("focus Options by A\n", error), "focus by a button that is not a direction");
  Check(InputScript::Parse("wait window Sheet timeout 5s\nwait focus Ok\n", error).has_value(), "wait window, focus");
  Check(InputScript::Parse("press A every 1s until window Inventory\n", error).has_value(), "press until window");
  Check(InputScript::Parse("dump_ui title-screen_1.x\ncapture title\ncapture\n", error).has_value(),
        "dump_ui NAME, capture [NAME]");
  Check(!InputScript::Parse("dump_ui ../x\n", error), "dump_ui: no paths");
  Check(!InputScript::Parse("dump_ui\n", error), "dump_ui needs a name");
  Check(!InputScript::Parse("capture a b\n", error), "capture takes one name");
  Check(MustParse("wait focus X\n").ReadsUi() && !MustParse("wait level_loaded\n").ReadsUi(), "ReadsUi");
}

void TestFocusPressesUntilFocused() {
  Run r("focus Back by UP every 10f\n");
  r.e.ui_reads = 1;
  r.e.focused = {"Play"};
  r.Frames(30);
  int presses = 0;
  for (size_t i = 1; i < r.pads.size(); ++i)
    if (r.pads[i].buttons == pad::kUp && r.pads[i - 1].buttons == 0) ++presses;
  Check(r.pads[0].buttons == pad::kUp, "the first press at once");
  Check(presses >= 2, "pressed again every 10 frames");
  Check(!r.script.done(), "still waiting");
  r.e.focused = {"12_back"};  // matches ignoring case and the layout prefix, from a fresh read only
  r.Frame();
  Check(!r.script.done(), "a stale read does not count");
  ++r.e.ui_reads;
  r.Frames(2);
  Check(r.script.done() && r.Has(Kind::kFinished), "done once a fresh read has the focus there");
}

void TestWaitWindowAndTimeout() {
  Run r("wait window Inventory timeout 1s\n");
  r.e.shown_windows = {"Inventory"};
  r.Frame();
  Check(!r.script.done(), "shown before the command began: not yet");
  ++r.e.ui_reads;
  r.Frames(2);
  Check(r.script.done() && !r.Has(Kind::kFailed), "seen on a fresh read");
  Run t("wait focus Nowhere timeout 1s\n");
  t.Frames(70);
  Check(t.Has(Kind::kFailed), "times out");
  bool says = false;
  for (const auto& a : t.actions) says |= a.kind == Kind::kFailed && a.text.find("focus on Nowhere") != std::string::npos;
  Check(says, "the failure names the focus it waited for");
  Run p("wait window Help\n");
  p.e.shown_windows = {"43_i_406", "4_Helper", "x_Help"};
  ++p.e.ui_reads;
  p.Frames(2);
  Check(!p.script.done(), "only a numeric prefix is dropped, and the rest must match whole");
}

void TestDumpAndNamedCapture() {
  Run r("capture title\ndump_ui title\npress A\n");
  r.Frame();
  Check(r.actions.size() == 1 && r.actions[0].kind == Kind::kCapture && r.actions[0].text == "title",
        "capture NAME");
  r.Frames(int(kCaptureFrames));
  Check(r.actions.size() == 2 && r.actions[1].kind == Kind::kDumpUi && r.actions[1].text == "title",
        "dump_ui NAME after the capture's still frames");
  r.Frames(10);
  bool pressed = false;
  for (const auto& p : r.pads) pressed |= p.buttons != 0;
  Check(!pressed && r.actions.size() == 2, "nothing pressed and no new action while the dump is pending");
  ++r.e.dumps_written;
  r.Frames(3);
  Check(r.pads.back().buttons == pad::kA || r.pads[r.pads.size() - 2].buttons == pad::kA, "on once written");
  Run t("dump_ui x\n");
  t.Frames(700);
  Check(t.Has(Kind::kFailed), "a dump never written fails the script");
  Run f("dump_ui x\npress A\n");
  f.Frames(3);
  ++f.e.dumps_failed;
  f.Frames(2);
  Check(f.Has(Kind::kFailed) && f.pads.back().buttons == 0, "a dump that could not be written fails at once");
}

void TestTouch() {
  std::string error;
  Check(!InputScript::Parse("touch 0 0.5 0.5\n", error), "finger 0");
  Check(!InputScript::Parse("touch 1 1.5 0.5\n", error), "outside the window");
  Check(!InputScript::Parse("touch 1 0.5\n", error), "touch needs X and Y");
  Check(!InputScript::Parse("release\n", error), "release needs the finger");
  Check(!InputScript::Parse("touch 1 Z\n", error), "an unknown control");
  Run n("touch 2 lb\n");
  n.Frame();
  Check(n.actions[0].kind == Kind::kTouch && n.actions[0].text == "lb" && n.actions[0].finger == 2, "touch on a control");
  Run u("touch 3 Up\n");
  u.Frame();
  Check(u.actions[0].kind == Kind::kTouch && u.actions[0].text == "Up", "touch on the d-pad's up");
  Run r("touch 1 0.2 0.8\ntouch 1 0.1 0.8\ntouch 2 0.9 0.85\nrelease 1\n");
  r.Frame();
  Check(r.actions.size() == 5 && r.actions[4].kind == Kind::kFinished, "touches act at once, one after another");
  const auto& down = r.actions[0];
  Check(down.kind == Kind::kTouch && down.finger == 1 && down.x == 0.2f && down.y == 0.8f && !down.release,
        "touch 1 at 0.2, 0.8");
  Check(r.actions[2].finger == 2 && r.actions[3].finger == 1 && r.actions[3].release, "finger 2, then 1 lifted");
  Check(r.pads.back() == PadState{}, "the script's own pad stays still");
}

// A recording of 4 s with a sample every 4 ms, far finer than a frame: lx is the sample's t_ms.
std::string FineRecording() {
  std::string text = "# x360ui pad replay v1\n# start_ms 500\n";
  for (int t = 0; t <= 4000; t += 4) text += std::to_string(t) + " " + std::to_string(t) + " 0 0 0 0\n";
  return text;
}

void TestReplay() {
  const std::string recording = FineRecording();
  const ScriptFileReader read = [&](std::string_view name) -> std::optional<std::string> {
    if (name == "route.txt") return recording;
    return std::nullopt;
  };
  std::string error;
  Check(!InputScript::Parse("replay missing.txt\n", error, read) && error.find("cannot read missing.txt") != std::string::npos,
        "replay of a file that cannot be read");
  Check(!InputScript::Parse("replay route.txt\n", error) && error.find("cannot read") != std::string::npos,
        "replay without a file reader");
  Check(!InputScript::Parse("replay route.txt trace\n", error, read), "replay FILE trace without its file");
  const ScriptFileReader bad = [](std::string_view) -> std::optional<std::string> { return "nonsense\n"; };
  Check(!InputScript::Parse("replay x.txt\n", error, bad) && error.find("x.txt: line 1") != std::string::npos,
        "a bad recording names its line");

  auto parsed = InputScript::Parse("replay route.txt trace out.csv\nquit\n", error, read);
  if (!parsed) return Check(false, "parse replay");
  InputScript script = std::move(*parsed);
  ScriptEvents e;
  std::vector<ScriptAction> actions;
  // 60 frames a second, starting off the 4 ms grid.
  const double frame_s = 1.0 / 60;
  e.seconds = 10.003;
  const double begin = e.seconds;
  bool drifted = false, rest_before = true, traced_before_t0 = false;
  int frames = 0, traced = 0;
  int64_t last_t_us = -1;
  while (!script.done() && frames < 2000) {
    const PadState p = script.Step(e, actions);
    const double t_ms = (e.seconds - begin) * 1000.0 - 500.0;
    const auto& f = script.replay_frame();
    if (t_ms < 0) {
      if (p.left_x != 0) rest_before = false;
      if (f) traced_before_t0 = true;
    } else if (f) {
      ++traced;
      // The sample due at this frame's time: the last one at or before it, on the 4 ms grid.
      const int expected = std::min(4000, int(t_ms) / 4 * 4);
      if (p.left_x != expected || f->pad.left_x != expected) drifted = true;
      if (f->trace != "out.csv" || f->t_us <= last_t_us) drifted = true;
      last_t_us = f->t_us;
    }
    ++e.frame;
    e.seconds += frame_s;
    ++frames;
  }
  Check(rest_before, "at rest during start_ms");
  Check(!traced_before_t0, "no trace frame before t=0");
  Check(!drifted, "every frame gets the sample due at its time (no drift), in time order");
  // From t=0 to 2 s after the last sample: 6 s at 60 frames a second, one frame either way.
  Check(traced >= 359 && traced <= 361, "traced frames: from t=0 to 2 s after the last sample");
  bool quit = false;
  for (const auto& a : actions) quit = quit || a.kind == Kind::kQuit;
  Check(script.done() && quit, "the next command follows the replay");
}

}  // namespace

int main() {
  TestReplay();
  TestTouch();
  TestUiCommandsParse();
  TestFocusPressesUntilFocused();
  TestWaitWindowAndTimeout();
  TestDumpAndNamedCapture();
  TestParseErrors();
  TestPressAndIdle();
  TestHoldStickAndTriggers();
  TestWaitLevelLoad();
  TestWaitTimeout();
  TestPressUntilMenu();
  TestPressFor();
  TestKeystrokes();
  if (failures) {
    std::printf("%d failure(s)\n", failures);
    return EXIT_FAILURE;
  }
  std::printf("input_script_test: all passed\n");
  return EXIT_SUCCESS;
}
