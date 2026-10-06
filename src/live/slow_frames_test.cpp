// Tests for SlowFrameDetector: which frames are reported, the previous frame in the line, the
// per-period line budget and the period summary.

#include "live/slow_frames.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace torchlight::live;

namespace {

int failures = 0;
void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}
bool Has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

FrameRecord Frame(uint64_t swap, double guest, double backend, double interval) {
  FrameRecord f;
  f.swap = swap;
  f.guest_ms = guest;
  f.backend_ms = backend;
  f.present_interval_ms = interval;
  return f;
}

}  // namespace

int main() {
  {
    SlowFrameDetector d;
    Check(!d.Add(Frame(1, 16.7, 10, 0)), "a normal frame is not reported");
    Check(d.TakePeriodSummary().empty(), "no summary without slow frames");
    auto line = d.Add(Frame(2, 40, 10, 16.7));
    Check(line && Has(*line, "by=guest") && Has(*line, "swap=2 guest=40.00"), "slow guest frame");
    Check(line && Has(*line, "previous: swap=1 "), "the previous frame is in the line");
    line = d.Add(Frame(3, 16.7, 30, 30));
    Check(line && Has(*line, "by=backend"), "slow backend frame");
    line = d.Add(Frame(4, 30, 30, 30));
    Check(line && Has(*line, "by=both"), "slow on both sides");
    line = d.Add(Frame(5, 16.7, 10, 34));
    Check(line && Has(*line, "by=interval"), "late present with both sides on time");
    FrameRecord dropped = Frame(6, 16.7, 10, 16.7);
    dropped.dropped = 1;
    dropped.worst_dropped_guest_ms = 50;
    line = d.Add(dropped);
    Check(line && Has(*line, "by=guest") && Has(*line, "worst_dropped=50.00"),
          "a slow frame dropped into this one counts as the guest's");
    FrameRecord untimed = Frame(7, 40, 10, 16.7);
    line = d.Add(untimed);
    Check(line && Has(*line, "recording=untimed"), "recording not timed on this frame");
    std::string summary = d.TakePeriodSummary();
    Check(Has(summary, ": 6 (guest 4, backend 2, present interval only 1), 0 not reported"),
          "period summary counts");
    Check(d.TakePeriodSummary().empty(), "the summary starts a new period");
  }
  {
    SlowFrameDetector d;
    size_t lines = 0;
    for (uint64_t i = 0; i < kSlowFrameReportsPerPeriod + 5; ++i) {
      if (d.Add(Frame(i, 40, 10, 40))) ++lines;
    }
    Check(lines == kSlowFrameReportsPerPeriod, "line budget per period");
    Check(Has(d.TakePeriodSummary(), "5 not reported"), "frames over the budget are counted");
    Check(bool(d.Add(Frame(100, 40, 10, 40))), "the budget renews with the period");
  }
  if (failures) return EXIT_FAILURE;
  std::puts("slow_frames_test: ok");
  return EXIT_SUCCESS;
}
