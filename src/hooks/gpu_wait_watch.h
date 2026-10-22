// A watch on the guest's waits for the GPU's progress (hooks/gpu_wait_hooks.cpp): when one wait
// lasts longer than kReportAfter, a report, then another every kRepeatEvery while it lasts; the
// wait itself goes on untouched. For hangs like the Intel startup one (docs/regressions.md, "Known,
// open"), so that one occurrence, on a player's machine too, says where it stopped.
//
// A wait is the steps of one guest thread that follow each other (less than kNewWaitGap between
// one step's end and the next one's start). The count starts again:
// - while the app is away on purpose and after it comes back (platform::SurfaceAwayCount changed
//   or is odd: Android's background; never on the desktop, where nothing blocks the guest);
// - after a step that took longer than kStoppedStep (a step sleeps ~0.2 ms: the process was
//   stopped, by the system's freezer, a debugger or a suspend, not waiting).
// No guest or SDK dependency: tested on its own (gpu_wait_watch_test.cpp).

#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

namespace torchlight::hooks {

class GpuWaitWatch {
 public:
  using Clock = std::chrono::steady_clock;
  static constexpr std::chrono::seconds kReportAfter{10};
  static constexpr std::chrono::seconds kRepeatEvery{60};
  static constexpr std::chrono::milliseconds kNewWaitGap{100};
  static constexpr std::chrono::seconds kStoppedStep{1};

  struct Report {
    std::chrono::nanoseconds waited{};
    uint64_t steps = 0;
    uint32_t progress_at_start = 0;  // the GPU's progress word at the wait's first step
    uint32_t progress_now = 0;
    uint64_t progress_changes = 0;   // how many steps saw it differ from the step before
    bool repeat = false;             // not the wait's first report
  };

  // One step, from `start` to `end`, that read `progress`; `away_count` is
  // platform::SurfaceAwayCount() at the step.
  std::optional<Report> Step(Clock::time_point start, Clock::time_point end, uint32_t progress,
                             uint64_t away_count);

 private:
  bool waiting_ = false;
  Clock::time_point wait_start_{}, last_end_{}, last_report_{};
  bool reported_ = false;
  uint64_t steps_ = 0, changes_ = 0, away_count_ = 0;
  uint32_t first_progress_ = 0, last_progress_ = 0;
};

}  // namespace torchlight::hooks
