#include "live/frame_timing.h"

#include <algorithm>

#include <fmt/format.h>
#include <rex/logging.h>

#include "live/guest_events.h"

namespace torchlight::live {

std::optional<double> FrameSeries::Add(Clock::time_point now, size_t dropped) {
  if (last_.time_since_epoch().count() == 0) {
    last_ = period_start_ = now;
    return std::nullopt;
  }
  const double ms = std::chrono::duration<double, std::milli>(now - last_).count();
  last_ = now;
  period_ms_.push_back(ms);
  period_dropped_ += dropped;
  std::lock_guard lock(recent_mutex_);
  recent_.push_back({now, float(ms), dropped});
  while (!recent_.empty() && now - recent_.front().time > kRecent) recent_.pop_front();
  return ms;
}

std::optional<std::string> FrameSeries::TakeSummary(Clock::time_point now, size_t* dropped) {
  if (period_ms_.empty() || now - period_start_ < kSummaryPeriod) return std::nullopt;
  std::sort(period_ms_.begin(), period_ms_.end());
  double sum = 0;
  for (double v : period_ms_) sum += v;
  auto pct = [&](double p) {
    return period_ms_[std::min(period_ms_.size() - 1, size_t(p * double(period_ms_.size())))];
  };
  // Frames past 33 and 50 ms: the long ones a percentile hides (two and three 60 Hz frames).
  const auto over = [&](double ms) {
    return period_ms_.end() - std::upper_bound(period_ms_.begin(), period_ms_.end(), ms);
  };
  std::string line = fmt::format(
      "{} frames, mean {:.2f} ms ({:.1f} fps), p95 {:.2f}, p99 {:.2f}, max {:.2f}, over 33 ms {}, "
      "over 50 ms {}",
      period_ms_.size(), sum / double(period_ms_.size()), 1000.0 * double(period_ms_.size()) / sum,
      pct(0.95), pct(0.99), period_ms_.back(), over(33.0), over(50.0));
  if (dropped) *dropped = period_dropped_;
  period_ms_.clear();
  period_dropped_ = 0;
  period_start_ = now;
  return line;
}

std::vector<float> FrameSeries::Recent(size_t* dropped) {
  const auto now = Clock::now();
  std::lock_guard lock(recent_mutex_);
  // A series that stopped (no presents) keeps nothing older than kRecent.
  while (!recent_.empty() && now - recent_.front().time > kRecent) recent_.pop_front();
  std::vector<float> out;
  out.reserve(recent_.size());
  size_t total_dropped = 0;
  for (const Entry& e : recent_) {
    out.push_back(e.ms);
    total_dropped += e.dropped;
  }
  if (dropped) *dropped = total_dropped;
  return out;
}

FrameTiming& FrameTiming::Get() {
  static FrameTiming timing;
  return timing;
}

void FrameTiming::OnSwap() {
  const auto now = FrameSeries::Clock::now();
  // The guest's events of the frame ending now (guest_events.h), logged when it was long.
  const GuestFrameEvents events = GuestEvents::Get().Take();
  if (const auto ms = guest_.Add(now); ms && *ms > kLongFrameMs)
    REXLOG_INFO("long frame: {}", DescribeLongFrame(*ms, events));
  if (auto summary = guest_.TakeSummary(now))
    REXLOG_INFO("frame time (guest swap to swap): {}", *summary);
}

void FrameTiming::OnPresent(size_t dropped) {
  const auto now = FrameSeries::Clock::now();
  presented_.Add(now, dropped);
  size_t period_dropped = 0;
  if (auto summary = presented_.TakeSummary(now, &period_dropped)) {
    REXLOG_INFO("frame time (presented, present to present): {}; {} guest frames dropped "
                "(backend behind)",
                *summary, period_dropped);
  }
}

void LogLevelLoad(std::chrono::milliseconds duration) {
  REXLOG_INFO("level load: {} ms", duration.count());
}

}  // namespace torchlight::live
