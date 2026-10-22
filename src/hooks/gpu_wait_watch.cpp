#include "hooks/gpu_wait_watch.h"

namespace torchlight::hooks {

std::optional<GpuWaitWatch::Report> GpuWaitWatch::Step(Clock::time_point start,
                                                       Clock::time_point end, uint32_t wait,
                                                       uint32_t progress, uint64_t away_count) {
  const bool away = (away_count & 1) != 0;
  const bool new_wait = !waiting_ || wait != wait_ || away || away_count != away_count_ ||
                        end - start >= kStoppedStep;
  away_count_ = away_count;
  wait_ = wait;
  if (new_wait) {
    waiting_ = !away;
    wait_start_ = end;
    reported_ = false;
    steps_ = 1;
    changes_ = 0;
    first_progress_ = last_progress_ = progress;
    return std::nullopt;
  }
  ++steps_;
  if (progress != last_progress_) ++changes_;
  last_progress_ = progress;
  if (end - wait_start_ < kReportAfter) return std::nullopt;
  if (reported_ && end - last_report_ < kRepeatEvery) return std::nullopt;
  Report r;
  r.waited = end - wait_start_;
  r.steps = steps_;
  r.progress_at_start = first_progress_;
  r.progress_now = progress;
  r.progress_changes = changes_;
  r.repeat = reported_;
  reported_ = true;
  last_report_ = end;
  return r;
}

}  // namespace torchlight::hooks
