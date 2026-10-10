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
  Check(r.actions.size() >= 3 && r.actions[0].kind == Kind::kMark &&
            r.actions[0].text == "DUNGEON: stand still" && r.actions[1].kind == Kind::kCapture &&
            r.actions[2].kind == Kind::kCommand && r.actions[2].text == "ASCEND",
        "the load ends the wait; mark, capture and command in the same frame");
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

}  // namespace

int main() {
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
