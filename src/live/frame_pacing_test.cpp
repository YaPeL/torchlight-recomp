// PacingCounter: refreshes each shown frame stayed on screen, the histogram and the even share.

#include <chrono>
#include <cstdio>
#include <string>

#include "live/frame_pacing.h"

namespace {
int g_failures = 0;
void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++g_failures;
  }
}
}  // namespace

int main() {
  using torchlight::live::PacingCounter;
  using namespace std::chrono_literals;
  const std::chrono::nanoseconds hz144 = 6944444ns, hz120 = 8333333ns;

  PacingCounter counter;
  auto s = counter.Take();
  Check(s.frames == 0 && s.mode == 0 && s.even_percent == 0, "nothing counted");
  Check(PacingCounter::Describe(s) == "no presentation times", "described with no frames");

  // 60 fps on 144 Hz: 2, 3, 2, 3, 2 refreshes (12 refreshes for 5 frames), with timing noise.
  for (int i = 0; i < 20; ++i) {
    counter.AddShown(hz144 * 2 + 300us, hz144);
    counter.AddShown(hz144 * 3 - 300us, hz144);
    counter.AddShown(hz144 * 2 - 300us, hz144);
    counter.AddShown(hz144 * 3 + 300us, hz144);
    counter.AddShown(hz144 * 2, hz144);
  }
  s = counter.Take();
  Check(s.frames == 100, "every frame counted");
  Check(s.held[2] == 60 && s.held[3] == 40, "2 and 3 refreshes");
  Check(s.mode == 2 && s.even_percent == 60.0, "60 % even");
  Check(PacingCounter::Describe(s) == "2x60.0% 3x40.0% (even 60.0% of 100)", "described");
  Check(counter.Take().frames == 0, "Take starts again");

  // 60 fps on 120 Hz, one frame repeated and one shown on the next refresh.
  for (int i = 0; i < 98; ++i) counter.AddShown(hz120 * 2, hz120);
  counter.AddShown(hz120 * 4, hz120);
  counter.AddShown(hz120, hz120);
  s = counter.Take();
  Check(s.mode == 2 && s.even_percent == 98.0, "98 % even at 120 Hz");
  Check(s.held[1] == 1 && s.held[4] == 1, "the late and the early frame");

  // Edges: a gap under half a refresh still is one; long hitches share the last bucket; an unknown
  // refresh or gap is not counted.
  counter.AddShown(1ms, hz120);
  counter.AddShown(hz120 * 7 - 1ms, hz120);
  counter.AddShown(500ms, hz120);
  counter.AddShown(hz120 * 2, 0ns);
  counter.AddShown(0ns, hz120);
  s = counter.Take();
  Check(s.frames == 3, "unknown refresh or gap not counted");
  Check(s.unknown_refresh == 1, "a frame with no refresh period is told apart");
  Check(s.held[1] == 1, "short gap is one refresh");
  Check(s.held[PacingCounter::kBuckets - 1] == 2, "hitches in the last bucket");
  Check(PacingCounter::Describe(s).find("7x66.7%+") != std::string::npos, "last bucket marked +");

  // A negative gap is not counted; half a refresh over rounds up (2.5 is 3).
  counter.AddShown(-hz120, hz120);
  counter.AddShown(20ms, 8ms);
  s = counter.Take();
  Check(s.frames == 1 && s.held[3] == 1, "negative gap skipped, 2.5 refreshes is 3");

  // A tie takes the smaller count; Peek leaves the count as it is.
  counter.AddShown(hz120, hz120);
  counter.AddShown(hz120 * 2, hz120);
  s = counter.Peek();
  Check(s.mode == 1 && s.even_percent == 50.0, "a tie takes the smaller count");
  Check(counter.Peek().frames == 2 && counter.Take().frames == 2, "Peek does not start again");

  // Frames shown without a refresh period only (variable refresh).
  counter.AddShown(hz120, 0ns);
  Check(PacingCounter::Describe(counter.Take()) == "no display refresh period (1 frame)",
        "described without a refresh period");

  if (g_failures) return 1;
  std::puts("frame_pacing_test: ok");
  return 0;
}
