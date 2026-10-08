#include "live/frame_timing.h"

#include <algorithm>

#include <rex/logging.h>

#include "live/guest_events.h"

namespace torchlight::live {

FrameTiming& FrameTiming::Get() {
  static FrameTiming timing;
  return timing;
}

void FrameTiming::OnSwap() {
  auto now = std::chrono::steady_clock::now();
  // The guest's events of the frame ending now (guest_events.h), logged when it was long.
  const GuestFrameEvents events = GuestEvents::Get().Take();
  if (last_swap_.time_since_epoch().count() != 0) {
    frame_ms_.push_back(std::chrono::duration<double, std::milli>(now - last_swap_).count());
    if (frame_ms_.back() > kLongFrameMs)
      REXLOG_INFO("long frame: {}", DescribeLongFrame(frame_ms_.back(), events));
    std::lock_guard lock(recent_mutex_);
    recent_.emplace_back(now, float(frame_ms_.back()));
    while (!recent_.empty() && now - recent_.front().first > kRecent) recent_.pop_front();
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

std::vector<float> FrameTiming::Recent() {
  std::lock_guard lock(recent_mutex_);
  std::vector<float> out;
  out.reserve(recent_.size());
  for (const auto& [time, ms] : recent_) out.push_back(ms);
  return out;
}

void LogLevelLoad(std::chrono::milliseconds duration) {
  REXLOG_INFO("level load: {} ms", duration.count());
}

}  // namespace torchlight::live
