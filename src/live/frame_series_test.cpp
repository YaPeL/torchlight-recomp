// FrameSeries: frame times and dropped frames, the 10 s summary for the log and the recent window
// for the frame counter.

#include <chrono>
#include <cstdio>
#include <string>

#include "live/frame_timing.h"

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
  using torchlight::live::FrameSeries;
  using namespace std::chrono_literals;
  FrameSeries series;
  // Recent() measures against the real clock, so the series ends now.
  const auto start = FrameSeries::Clock::now() - 12s;
  Check(!series.Add(start), "the first frame only starts the series");
  auto t = start;
  // 100 frames of 10 ms, 2 guest frames dropped before every tenth.
  for (int i = 1; i <= 100; ++i) {
    t += 10ms;
    const auto ms = series.Add(t, i % 10 == 0 ? 2 : 0);
    Check(ms && *ms > 9.99 && *ms < 10.01, "each frame's time");
  }
  Check(!series.TakeSummary(t), "no summary before 10 s");
  t += 10s;  // one long frame closes the period
  series.Add(t);
  size_t dropped = 0;
  auto summary = series.TakeSummary(t, &dropped);
  Check(summary.has_value(), "a summary after 10 s");
  Check(dropped == 20, "the period's dropped frames");
  Check(summary && summary->starts_with("101 frames,"), "the period's frame count");
  Check(summary && summary->find("max 10000.00") != std::string::npos, "the long frame is the max");
  Check(summary && summary->find("over 33 ms 1, over 50 ms 1") != std::string::npos,
        "the long frame counted");
  Check(!series.TakeSummary(t), "the next period starts empty");
  // The recent window: only the frames of the last 10 s (here the long one, ending now), with
  // the frames dropped in them.
  size_t recent_dropped = 99;
  const auto recent = series.Recent(&recent_dropped);
  Check(recent.size() == 1 && recent_dropped == 0, "older frames leave the recent window");
  FrameSeries idle;
  Check(idle.Recent().empty(), "a series with no frames shows nothing");
  if (g_failures == 0) std::printf("frame series test: ok\n");
  return g_failures == 0 ? 0 : 1;
}
