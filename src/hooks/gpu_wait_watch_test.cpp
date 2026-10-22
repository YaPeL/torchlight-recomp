// GpuWaitWatch (gpu_wait_watch.h): when a guest wait for the GPU is reported, and when the count
// starts again.
#include <chrono>
#include <cstdio>

#include "hooks/gpu_wait_watch.h"

using torchlight::hooks::GpuWaitWatch;
using namespace std::chrono_literals;

namespace {

int failures = 0;
void Check(bool ok, const char* what) {
  if (ok) return;
  ++failures;
  std::fprintf(stderr, "FAIL: %s\n", what);
}

// Steps of 0.3 ms back to back of the wait marked `wait`, as the hook makes them, from `t` for
// `length`; the reports seen.
struct Run {
  int reports = 0;
  int repeats = 0;
  GpuWaitWatch::Report last{};
};
Run Steps(GpuWaitWatch& w, GpuWaitWatch::Clock::time_point& t, std::chrono::nanoseconds length,
          uint32_t wait = 1, uint64_t away = 0, uint32_t progress = 7, bool moving = false) {
  Run run;
  const auto end = t + length;
  uint32_t p = progress;
  while (t < end) {
    const auto start = t;
    t += 300us;
    if (moving) ++p;
    if (auto r = w.Step(start, t, wait, p, away)) {
      ++run.reports;
      run.repeats += r->repeat;
      run.last = *r;
    }
  }
  return run;
}

}  // namespace

int main() {
  const GpuWaitWatch::Clock::time_point t0{};
  // A normal wait (a frame) and a long one under 10 s: nothing.
  {
    GpuWaitWatch w;
    auto t = t0;
    Check(Steps(w, t, 17ms, 1).reports == 0, "a frame's wait");
    Check(Steps(w, t, 9s, 2).reports == 0, "9 s");
  }
  // Stuck: one report past 10 s with the progress word still, then one a minute.
  {
    GpuWaitWatch w;
    auto t = t0;
    Run r = Steps(w, t, 11s);
    Check(r.reports == 1 && r.repeats == 0, "one report past 10 s");
    Check(r.last.waited >= 10s && r.last.waited < 10s + 1ms, "reported at 10 s");
    Check(r.last.progress_at_start == 7 && r.last.progress_now == 7 && r.last.progress_changes == 0,
          "the progress word did not move");
    r = Steps(w, t, 120s);
    Check(r.reports == 2 && r.repeats == 2, "then once a minute");
  }
  // The progress word moving without reaching the target is told apart.
  {
    GpuWaitWatch w;
    auto t = t0;
    Run r = Steps(w, t, 11s, 1, 0, 100, true);
    Check(r.reports == 1 && r.last.progress_changes > 0 && r.last.progress_now != 100,
          "a moving progress word");
  }
  // With vsync: a wait in every frame's swap, 6 ms of guest work apart, for a minute. Each is its
  // own wait (the guest's mark changes), so they never add up.
  {
    GpuWaitWatch w;
    auto t = t0;
    int reports = 0;
    for (uint32_t i = 1; i <= 6000; ++i) {
      reports += Steps(w, t, 4ms, i).reports;
      t += 6ms;
    }
    Check(reports == 0, "a wait per frame");
  }
  // One wait whose steps come with gaps between them still counts as one.
  {
    GpuWaitWatch w;
    auto t = t0;
    int reports = 0;
    for (int i = 0; i < 120; ++i) {
      reports += Steps(w, t, 10ms, 9).reports;
      t += 100ms;
    }
    Check(reports == 1, "one wait with gaps");
  }
  // Away on purpose (Android's background): quiet while away, the count starts again on return.
  {
    GpuWaitWatch w;
    auto t = t0;
    Check(Steps(w, t, 8s).reports == 0, "before going away");
    Check(Steps(w, t, 60s, 1, 1).reports == 0, "away");
    Check(Steps(w, t, 9s, 1, 2).reports == 0, "back: counted from the return");
    Check(Steps(w, t, 2s, 1, 2).reports == 1, "back and stuck for 11 s");
  }
  // Away and back between two steps, unseen while away: the count still starts again.
  {
    GpuWaitWatch w;
    auto t = t0;
    Check(Steps(w, t, 8s).reports == 0 && Steps(w, t, 8s, 1, 2).reports == 0,
          "away and back unseen");
  }
  // A step that took seconds (the process stopped): the count starts again.
  {
    GpuWaitWatch w;
    auto t = t0;
    Check(Steps(w, t, 8s).reports == 0, "before the stop");
    const auto start = t;
    t += 30s;
    Check(!w.Step(start, t, 1, 7, 0), "the stopped step");
    Check(Steps(w, t, 8s).reports == 0, "after the stop: counted from it");
  }
  if (failures == 0) std::printf("gpu_wait_watch_test: ok\n");
  return failures == 0 ? 0 : 1;
}
