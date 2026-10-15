#include "live/frame_pacer.h"

namespace torchlight::live {

void FramePacer::SetPeriod(Clock::duration period) {
  if (period == period_) return;
  period_ = period;
  next_ = {};
}

FramePacer::Clock::time_point FramePacer::Slot(Clock::time_point ready) {
  if (period_ <= Clock::duration::zero()) return ready;
  if (next_ == Clock::time_point{} || ready > next_) {
    // The first frame, or one ready after its slot (late, or after a stall such as a level load):
    // presented now, and the slots follow from it.
    next_ = ready;
  } else if (next_ - ready > kEarlyMargin) {
    next_ -= kEarlierStep;
  }
  const Clock::time_point slot = next_;
  next_ += period_;
  return slot;
}

}  // namespace torchlight::live
