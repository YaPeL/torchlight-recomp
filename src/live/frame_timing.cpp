#include "live/frame_timing.h"
#include "live/perf_hud.h"

#include <algorithm>

#include <rex/logging.h>

namespace torchlight::live {

FrameTiming& FrameTiming::Get() {
  static FrameTiming timing;
  return timing;
}

void FrameTiming::OnSwap() {
  auto now = std::chrono::steady_clock::now();
  if (last_swap_.time_since_epoch().count() != 0) {
    frame_ms_.push_back(std::chrono::duration<double, std::milli>(now - last_swap_).count());
    perf::OnSwap(frame_ms_.back());  // TEMPORARY (perf_hud.h)
  } else {
    last_summary_ = now;
  }
  last_swap_ = now;
  if (now - last_summary_ < std::chrono::seconds(10) || frame_ms_.empty()) return;
  std::sort(frame_ms_.begin(), frame_ms_.end());
  double sum = 0;
  for (double v : frame_ms_) sum += v;
  auto pct = [&](double p) {
    return frame_ms_[std::min(frame_ms_.size() - 1, size_t(p * frame_ms_.size()))];
  };
  // Frames past 33 and 50 ms: the long ones a percentile hides (two and three 60 Hz frames).
  const auto over = [&](double ms) {
    return frame_ms_.end() - std::upper_bound(frame_ms_.begin(), frame_ms_.end(), ms);
  };
  REXLOG_INFO("frame time (guest swap to swap): {} frames, mean {:.2f} ms ({:.1f} fps), p95 "
              "{:.2f}, p99 {:.2f}, max {:.2f}, over 33 ms {}, over 50 ms {}",
              frame_ms_.size(), sum / frame_ms_.size(), 1000.0 * frame_ms_.size() / sum,
              pct(0.95), pct(0.99), frame_ms_.back(), over(33.0), over(50.0));
  frame_ms_.clear();
  last_summary_ = now;
}

void LogLevelLoad(std::chrono::milliseconds duration) {
  REXLOG_INFO("level load: {} ms", duration.count());
  perf::OnLevelLoaded();  // TEMPORARY (perf_hud.h)
}

}  // namespace torchlight::live
