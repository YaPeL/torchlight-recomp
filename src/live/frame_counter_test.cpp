// SummarizeFrames: what the frame counter shows from the last seconds' frame times.

#include <cmath>
#include <cstdio>
#include <vector>

#include "live/frame_counter.h"

namespace {
int g_failures = 0;
void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++g_failures;
  }
}
bool Near(double a, double b) { return std::fabs(a - b) < 0.01; }
}  // namespace

int main() {
  using torchlight::live::SummarizeFrames;

  const auto empty = SummarizeFrames({});
  Check(empty.frames == 0 && empty.fps == 0 && empty.max_ms == 0, "no frames: all zero");

  // Steady 60 fps for 10 s.
  std::vector<float> steady(600, 1000.0f / 60.0f);
  const auto s = SummarizeFrames(steady);
  Check(Near(s.fps, 60.0), "steady: 60 fps over the last second");
  Check(Near(s.low_1pct_fps, 60.0) && Near(s.max_ms, 1000.0 / 60.0), "steady: 1% low and max");
  Check(s.over_33 == 0 && s.over_50 == 0, "steady: no long frames");

  // The same with three long frames long ago (40, 60 and 120 ms) and fast frames in the last second.
  std::vector<float> mixed(400, 10.0f);
  mixed[50] = 40.0f;
  mixed[100] = 60.0f;
  mixed[150] = 120.0f;
  for (int i = 0; i < 200; ++i) mixed.push_back(5.0f);
  const auto m = SummarizeFrames(mixed);
  Check(Near(m.fps, 200.0), "the last second only: 200 fps");
  Check(Near(m.last_ms, 5.0), "the last frame");
  Check(Near(m.max_ms, 120.0), "the longest frame of all of them");
  Check(m.over_33 == 3 && m.over_50 == 2, "frames past 33 and 50 ms");
  Check(Near(m.low_1pct_fps, 100.0), "1% low: the 99th percentile frame (10 ms) as a rate");

  if (g_failures) return 1;
  std::printf("frame_counter_test: ok\n");
  return 0;
}
