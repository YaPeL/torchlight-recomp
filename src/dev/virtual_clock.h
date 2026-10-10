// The game's clock in deterministic development runs (dev/deterministic_time.h): it moves a fixed
// step per guest frame instead of with real time, so two runs of the same input script see the
// same frame times.
//
// Each thread also sees the clock move by one tick per read within a frame: a loop that waits for
// time to pass still ends, and a thread that makes the same reads gets the same values in every
// run. Values are monotonic per thread.

#pragma once

#include <atomic>
#include <cstdint>

namespace torchlight::dev {

class VirtualClock {
 public:
  // What one thread has read: its last value.
  struct Reader {
    uint64_t last = 0;
    bool any = false;  // read before
  };

  VirtualClock(uint64_t start_ticks, uint64_t ticks_per_frame)
      : start_(start_ticks), step_(ticks_per_frame) {}

  // One guest frame.
  void Advance() { frame_.fetch_add(1, std::memory_order_relaxed); }
  uint64_t frame() const { return frame_.load(std::memory_order_relaxed); }
  // The frame's time, in ticks.
  uint64_t FrameTicks() const { return start_ + frame() * step_; }
  // The clock as `reader`'s thread sees it.
  uint64_t Read(Reader& reader) const;

 private:
  uint64_t start_, step_;
  std::atomic<uint64_t> frame_{0};
};

}  // namespace torchlight::dev
