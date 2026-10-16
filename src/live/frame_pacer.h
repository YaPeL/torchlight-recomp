// FramePacer: when to present each frame so that presents come one fps cap period apart.
//
// With a frame rate cap the guest's vblank is regular, but each frame reaches the present after a
// varying amount of work (the guest's frame after its vblank, then the backend's), so presenting
// as soon as a frame is ready spreads the presents by a couple of milliseconds around the period.
// A variable refresh display (G-Sync, FreeSync) shows each frame when it is presented, so that
// spread is on the screen as uneven motion (docs/performance-profile.md, frame pacing). The pacer
// holds each frame until its slot: the previous slot plus the period. A frame ready after its
// slot is presented at once and the slots move later with it. Every kWindow frames, when even the
// frame that waited least waited more than kEarlyMargin, the slots move earlier by the excess, so
// the added latency comes back to what the frames' spread needs within a fraction of a second.
#pragma once

#include <chrono>

namespace torchlight::live {

class FramePacer {
 public:
  using Clock = std::chrono::steady_clock;

  // The fps cap's period; zero: no pacing (every frame is presented when ready).
  void SetPeriod(Clock::duration period);
  Clock::duration period() const { return period_; }

  // When to present a frame ready at `ready` (never before it).
  Clock::time_point Slot(Clock::time_point ready);

  // The wait kept ahead of the frame that waits least in a window.
  static constexpr Clock::duration kEarlyMargin = std::chrono::microseconds(1000);
  static constexpr int kWindow = 60;

 private:
  Clock::duration period_{};
  Clock::time_point next_{};
  Clock::duration least_wait_ = Clock::duration::max();
  int frames_ = 0;
};

}  // namespace torchlight::live
