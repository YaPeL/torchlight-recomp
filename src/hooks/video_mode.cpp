#include "hooks/video_mode.h"

#include <algorithm>
#include <cmath>

namespace torchlight::hooks {

std::optional<FrameSize> FrameSizeForVideoMode(uint32_t width, uint32_t height) {
  if (!width || !height) return std::nullopt;
  const double aspect = double(width) / double(height);
  auto near = [&](double target) { return std::fabs(aspect - target) < target * 0.01; };
  if (aspect <= 4.0 / 3.0 * 1.01 || near(16.0 / 9.0)) return std::nullopt;
  if (near(16.0 / 10.0)) return FrameSize{1280, 800};
  constexpr uint32_t kTile = 80, kMaxWidth = 2560, kHeight = 720;
  const uint32_t tiles = uint32_t(std::lround(kHeight * aspect / kTile));
  return FrameSize{std::clamp(tiles * kTile, kTile, kMaxWidth), kHeight};
}

FrameSize VideoModeForAspect(double aspect) {
  constexpr uint32_t kHeight = 720;
  if (!(aspect > 0)) return FrameSize{1280, kHeight};
  if (aspect <= 4.0 / 3.0 * 1.01) return FrameSize{956, kHeight};
  if (aspect >= 32.0 / 9.0) return FrameSize{2560, kHeight};
  return FrameSize{uint32_t(std::lround(kHeight * aspect)), kHeight};
}

}  // namespace torchlight::hooks
