#include "live/frame_pacing.h"

#include <cmath>
#include <cstdio>

namespace torchlight::live {

void PacingCounter::AddShown(std::chrono::nanoseconds gap, std::chrono::nanoseconds refresh) {
  if (gap.count() <= 0) return;
  if (refresh.count() <= 0) {
    ++current_.unknown_refresh;
    return;
  }
  const double refreshes = double(gap.count()) / double(refresh.count());
  size_t held = refreshes >= double(kBuckets - 1) ? kBuckets - 1 : size_t(std::llround(refreshes));
  if (held < 1) held = 1;
  ++current_.held[held];
  ++current_.frames;
}

PacingCounter::Summary PacingCounter::Take() {
  const Summary s = Peek();
  current_ = {};
  return s;
}

PacingCounter::Summary PacingCounter::Peek() const {
  Summary s = current_;
  size_t best = 0;
  for (size_t n = 1; n < kBuckets; ++n) {
    if (s.held[n] > best) {
      best = s.held[n];
      s.mode = uint32_t(n);
    }
  }
  s.even_percent = s.frames ? 100.0 * double(best) / double(s.frames) : 0.0;
  return s;
}

std::string PacingCounter::Describe(const Summary& s) {
  if (!s.frames && s.unknown_refresh) {
    return "no display refresh period (" + std::to_string(s.unknown_refresh) +
           (s.unknown_refresh == 1 ? " frame)" : " frames)");
  }
  if (!s.frames) return "no presentation times";
  std::string out;
  char buf[48];
  for (size_t n = 1; n < kBuckets; ++n) {
    if (!s.held[n]) continue;
    std::snprintf(buf, sizeof buf, "%s%zux%.1f%%", out.empty() ? "" : " ", n,
                  100.0 * double(s.held[n]) / double(s.frames));
    out += buf;
    if (n == kBuckets - 1) out += "+";
  }
  std::snprintf(buf, sizeof buf, " (even %.1f%% of %zu)", s.even_percent, s.frames);
  return out + buf;
}

}  // namespace torchlight::live
