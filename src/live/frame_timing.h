// Guest frame time (swap to swap), always measured and summarised in the log every 10 s, with
// or without the live mode, so the two can be compared; a frame past kLongFrameMs gets its own
// "long frame:" line with what the guest did in it (guest_events.h; in the native mode the live
// mode's "slow frame" line has the backend's side); and the game's level loads, timed by the
// level load hook (achievements/guest_hooks.cpp). docs/performance-profile.md uses both.

#pragma once

#include <chrono>
#include <deque>
#include <mutex>
#include <vector>

namespace torchlight::live {

class FrameTiming {
 public:
  static FrameTiming& Get();
  // Guest render thread, at every device swap.
  void OnSwap();
  // The frame times (ms) of the last kRecent, oldest first, for the frame counter (any thread).
  static constexpr std::chrono::seconds kRecent{10};
  std::vector<float> Recent();

 private:
  FrameTiming() = default;
  std::chrono::steady_clock::time_point last_swap_{}, last_summary_{};
  std::vector<double> frame_ms_;
  std::mutex recent_mutex_;
  std::deque<std::pair<std::chrono::steady_clock::time_point, float>> recent_;
};

// "level load: N ms" in the log: one call of the guest's level load (guest_abi kLevelLoad).
void LogLevelLoad(std::chrono::milliseconds duration);

}  // namespace torchlight::live
