#include "hooks/adaptive_vsync.h"

#include <algorithm>
#include <atomic>

#include "guest_abi/adaptive_vsync.h"

namespace torchlight::hooks {

namespace {
std::atomic<uint32_t> g_fps_cap{0};
}  // namespace

void SetAdaptiveVsyncCap(uint32_t fps_cap) { g_fps_cap.store(fps_cap, std::memory_order_relaxed); }
uint32_t AdaptiveVsyncCap() { return g_fps_cap.load(std::memory_order_relaxed); }

uint32_t AdaptiveVsyncInterval(uint32_t game_interval, float last_fps, uint32_t fps_cap) {
  namespace abi = guest_abi::adaptive_vsync;
  if (fps_cap == 0) return game_interval;
  const float threshold = std::min(abi::kThresholdFps, float(fps_cap) - 1.0f);
  return last_fps < threshold ? abi::kIntervalImmediate : abi::kIntervalDefault;
}

}  // namespace torchlight::hooks
