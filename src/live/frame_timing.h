// Frame times, always measured and summarised in the log every 10 s, with or without the live mode,
// so the two can be compared:
// - the guest's (swap to swap): how fast the game itself runs; a frame past kLongFrameMs gets its
//   own "long frame:" line with what the guest did in it (guest_events.h; in the native mode the
//   live mode's "slow frame" line has the backend's side);
// - in the native mode, what reaches the screen (present to present, the backend thread) and the
//   guest frames dropped because the backend was still busy with an earlier one: when the backend
//   is the limit these are fewer than the guest's.
// And the game's level loads, timed by the level load hook (achievements/guest_hooks.cpp).
// docs/performance-profile.md uses them.

#pragma once

#include <chrono>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace torchlight::live {

// One series of frame times: a 10 s summary for the log and the last kRecent seconds for the frame
// counter. Add and TakeSummary from one thread; Recent from any.
class FrameSeries {
 public:
  using Clock = std::chrono::steady_clock;
  static constexpr std::chrono::seconds kRecent{10};
  static constexpr std::chrono::seconds kSummaryPeriod{10};

  // A frame that ended at `now`, `dropped` frames skipped before it (counted, not timed). The
  // first call only starts the series. Returns the frame's time (ms), if there was one.
  std::optional<double> Add(Clock::time_point now, size_t dropped = 0);
  // Every kSummaryPeriod: the period's summary ("N frames, mean ... ms (... fps), p95 ..., p99 ...,
  // max ..., over 33 ms N, over 50 ms N"), and the frames dropped in it; then the period starts
  // over.
  std::optional<std::string> TakeSummary(Clock::time_point now, size_t* dropped = nullptr);
  // The frame times (ms) of the last kRecent, oldest first, and the frames dropped in them.
  std::vector<float> Recent(size_t* dropped = nullptr);

 private:
  Clock::time_point last_{}, period_start_{};
  std::vector<double> period_ms_;
  size_t period_dropped_ = 0;
  std::mutex recent_mutex_;
  struct Entry {
    Clock::time_point time;
    float ms;
    size_t dropped;
  };
  std::deque<Entry> recent_;
};

class FrameTiming {
 public:
  static FrameTiming& Get();
  static constexpr std::chrono::seconds kRecent = FrameSeries::kRecent;
  // Guest render thread, at every device swap.
  void OnSwap();
  // Live mode's backend thread, after every present that reached the window: `dropped` guest
  // frames were skipped since the previous one (the backend was behind).
  void OnPresent(size_t dropped);
  // The last kRecent seconds, oldest first, for the frame counter (any thread).
  std::vector<float> Recent() { return guest_.Recent(); }
  // Empty unless frames are being presented (the native mode).
  std::vector<float> RecentPresented(size_t* dropped) { return presented_.Recent(dropped); }

 private:
  FrameTiming() = default;
  FrameSeries guest_, presented_;
};

// "level load: N ms" in the log: one call of the guest's level load (guest_abi kLevelLoad).
void LogLevelLoad(std::chrono::milliseconds duration);

}  // namespace torchlight::live
