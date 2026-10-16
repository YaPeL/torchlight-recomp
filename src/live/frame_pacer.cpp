#include "live/frame_pacer.h"

#include <algorithm>

namespace torchlight::live {

void FramePacer::SetPeriod(Clock::duration period) {
  if (period == period_) return;
  period_ = period;
  next_ = {};
  least_wait_ = Clock::duration::max();
  frames_ = 0;
}

FramePacer::Clock::time_point FramePacer::Slot(Clock::time_point ready) {
  if (period_ <= Clock::duration::zero()) return ready;
  if (next_ == Clock::time_point{} || ready > next_) {
    // The first frame, or one ready after its slot (late, or after a stall such as a level load):
    // presented now, and the slots follow from it.
    next_ = ready;
  }
  least_wait_ = std::min(least_wait_, next_ - ready);
  const Clock::time_point slot = next_;
  next_ += period_;
  if (++frames_ == kWindow) {
    // Every frame of the window waited at least least_wait_: give back what exceeds the margin.
    if (least_wait_ > kEarlyMargin) next_ -= least_wait_ - kEarlyMargin;
    least_wait_ = Clock::duration::max();
    frames_ = 0;
  }
  return slot;
}

}  // namespace torchlight::live
