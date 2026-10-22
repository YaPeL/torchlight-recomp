// A watch on the guest's waits for the GPU's progress (hooks/gpu_wait_hooks.cpp): when one wait
// lasts longer than kReportAfter, a report, then another every kRepeatEvery while it lasts; the
// wait itself goes on untouched. For hangs like the Intel startup one (docs/regressions.md, "Known,
// open"), so that one occurrence, on a player's machine too, says where it stopped.
//
// A wait is the steps of one guest thread that share the guest's own mark of its beginning (the
// time base its begin function stores, xbox_d3d::gpu_wait::kBeganTimeBase): with vsync the guest
// waits in every frame's swap, a few milliseconds apart, and each of those is a wait of its own.
// The count also starts again:
// - while the app is away (platform::SurfaceAwayCount changed or is odd: Android's background,
//   where the system may slow down or stop the app's threads; never on the desktop);
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
  static constexpr std::chrono::seconds kStoppedStep{1};

  struct Report {
    std::chrono::nanoseconds waited{};
    uint64_t steps = 0;
    uint32_t progress_at_start = 0;  // the GPU's progress word at the wait's first step
    uint32_t progress_now = 0;
    uint64_t progress_changes = 0;   // how many steps saw it differ from the step before
    bool repeat = false;             // not the wait's first report
  };

  // One step, from `start` to `end`, of the wait marked `wait` (its kBeganTimeBase), that read
  // `progress`; `away_count` is platform::SurfaceAwayCount() at the step.
  std::optional<Report> Step(Clock::time_point start, Clock::time_point end, uint32_t wait,
                             uint32_t progress, uint64_t away_count);

 private:
  bool waiting_ = false;
  Clock::time_point wait_start_{}, last_report_{};
  uint32_t wait_ = 0;
  bool reported_ = false;
  uint64_t steps_ = 0, changes_ = 0, away_count_ = 0;
  uint32_t first_progress_ = 0, last_progress_ = 0;
};

}  // namespace torchlight::hooks
