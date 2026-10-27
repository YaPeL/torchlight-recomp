// When each present reached the screen, as the window system reports it: the measurement of the
// vsync latency (docs/performance-profile.md) and the even frames (live/frame_pacing.h).
// Development builds with TORCHLIGHT_PRESENTATION_TIME on Wayland (the presentation-time protocol)
// and Android (EGL_ANDROID_get_frame_timestamps); null elsewhere. Without it the live mode measures
// up to the present's return, which is not the screen when the driver queues frames.

#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

#include "platform/platform.h"

namespace torchlight::platform {

class PresentationClock {
 public:
  virtual ~PresentationClock() = default;
  // Backend thread, right before the present that commits frame `id`.
  virtual void BeforePresent(uint64_t id) = 0;
  // Backend thread: the frames the display showed since the last call, and when (steady_clock).
  // Frames the compositor never showed (replaced by a later one) are left out.
  struct Shown {
    uint64_t id;
    std::chrono::steady_clock::time_point time;  // the refresh that showed it (its vblank)
    std::chrono::nanoseconds refresh{0};         // the display's refresh period; 0 if unknown
  };
  virtual std::vector<Shown> TakeShown() = 0;
};

// The backend's window: null where the window system or the build gives no presentation times.
std::unique_ptr<PresentationClock> CreatePresentationClock(const NativeWindow& window);

}  // namespace torchlight::platform
