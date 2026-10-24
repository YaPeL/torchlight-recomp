// The recording and the trace (pad_replay.h): parsing and its errors, the sample in effect at a
// time, and the trace's lines.

#include "dev/pad_replay.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

using namespace torchlight::dev;

int failures = 0;
void Check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

void TestParse() {
  std::string error;
  const auto r = PadReplay::Parse(
      "# x360ui pad replay v1\r\n# start_ms 3000\r\n# pad_record: a comment\r\n0 0 0 0 0 0\r\n"
      "4098 0 0 0 0 4096\r\n5653 -588 120 7 255 0\r\n\r\n5653 -32768 32767 0 0 0\r\n",
      error);
  Check(r.has_value(), "a recording with CRLF, comments and a blank line parses");
  if (!r) return;
  Check(r->start_ms == 3000, "start_ms");
  Check(r->samples.size() == 4 && r->last_ms() == 5653, "four samples, the last at 5653 ms");
  const auto& s = r->samples[2].pad;
  Check(s.left_x == -588 && s.left_y == 120 && s.left_trigger == 7 && s.right_trigger == 255 && s.buttons == 0,
        "stick, triggers and buttons of a sample");
  Check(r->samples[1].pad.buttons == 0x1000, "4096 is A");
  Check(r->samples[3].pad.left_x == -32768 && r->samples[3].pad.left_y == 32767, "the axes' ends");
  Check(!PadReplay::Parse("0 0 0 0 0 0\n", error), "no header: refused");
  Check(error.find("line 1") != std::string::npos, "no header: line 1");
}

void TestParseErrors() {
  std::string error;
  const auto refused = [&](const char* body, const char* line, const char* what) {
    const std::string text = std::string("# x360ui pad replay v1\n") + body;
    Check(!PadReplay::Parse(text, error) && error.find(line) != std::string::npos, what);
  };
  refused("", "no samples", "header only");
  refused("10 0 0 0 0 0\n5 0 0 0 0 0\n", "line 3", "t_ms goes back");
  refused("0 40000 0 0 0 0\n", "line 2", "lx out of range");
  refused("0 0 0 256 0 0\n", "line 2", "lt out of range");
  refused("0 0 0 0 0 65536\n", "line 2", "buttons out of range");
  refused("0 0 0 0 0\n", "line 2", "five numbers");
  refused("0 0 0.5 0 0 0\n", "line 2", "not an integer");
  refused("-1 0 0 0 0 0\n", "line 2", "negative t_ms");
  refused("# start_ms x\n0 0 0 0 0 0\n", "line 2", "bad start_ms");
  Check(!PadReplay::Parse("", error), "empty text");
}

void TestAt() {
  std::string error;
  const auto r = PadReplay::Parse("# x360ui pad replay v1\n100 10 0 0 0 0\n200 20 0 0 0 0\n200 25 0 0 0 0\n", error);
  if (!r) return Check(false, "parse for At");
  Check(r->At(0).left_x == 0, "before the first sample: at rest");
  Check(r->At(99.9).left_x == 0, "just before the first sample");
  Check(r->At(100).left_x == 10, "at the first sample");
  Check(r->At(199.9).left_x == 10, "held until the next");
  Check(r->At(200).left_x == 25, "two samples at one time: the later wins");
  Check(r->At(1e9).left_x == 25, "the last one holds to the end");
}

void TestTrace() {
  Check(HeroTraceHeader() ==
            "# x360ui pad trace v1 recomp\nt_us,frame,x_mu,z_mu,fx_mu,fz_mu,scale_mu,speed_mu,lx,ly\n",
        "header");
  HeroTraceRow row;
  row.t_us = 16667;
  row.frame = 42;
  row.x = -31.9814f;
  row.z = 17.1536f;
  row.fx = 0.545f;
  row.fz = -0.8394f;
  row.scale = 1.0f;
  row.speed = 4.5f;
  row.lx = -32767;
  row.ly = 120;
  const std::string line = HeroTraceLine(row);
  Check(line == "16667,42,-31981,17154,545,-839,1000,4500,-32767,120\n", "a line: x1000, rounded, integers");
}

}  // namespace

int main() {
  TestParse();
  TestParseErrors();
  TestAt();
  TestTrace();
  if (failures) {
    std::printf("%d failure(s)\n", failures);
    return EXIT_FAILURE;
  }
  std::printf("pad_replay_test: all passed\n");
  return EXIT_SUCCESS;
}
