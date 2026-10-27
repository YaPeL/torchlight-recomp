// Presentation times on Android (presentation_time.h): EGL_ANDROID_get_frame_timestamps on the
// EGL surface OGRE draws in. The backend thread holds that surface's context, so the display and
// surface are the current ones (OGRE's window gives no surface handle); the surface changes when
// the activity gives a new one back, and the frames of the old one are dropped.
// Timestamps are CLOCK_MONOTONIC nanoseconds, steady_clock's clock with bionic's libc++.

#include "platform/presentation_time.h"

#include <cstring>
#include <deque>
#include <utility>

#include <EGL/egl.h>
#include <EGL/eglext.h>

namespace torchlight::platform {

namespace {

class AndroidPresentationClock final : public PresentationClock {
 public:
  void BeforePresent(uint64_t id) override {
    if (!Current()) return;
    EGLuint64KHR frame = 0;
    if (!get_next_frame_id_(display_, surface_, &frame)) return;
    // A surface that never shows (the activity in the background): no unbounded growth.
    if (pending_.size() >= 64) pending_.pop_front();
    pending_.emplace_back(frame, id);
  }

  std::vector<Shown> TakeShown() override {
    std::vector<Shown> shown;
    if (!Current() || pending_.empty()) return shown;
    const EGLint interval_name = EGL_COMPOSITE_INTERVAL_ANDROID;
    EGLnsecsANDROID interval = 0;
    if (!get_compositor_timing_(display_, surface_, 1, &interval_name, &interval) || interval < 0) {
      interval = 0;
    }
    const EGLint present_name = EGL_DISPLAY_PRESENT_TIME_ANDROID;
    while (!pending_.empty()) {
      EGLnsecsANDROID present = EGL_TIMESTAMP_INVALID_ANDROID;
      // Fails for a frame older than the surface's history: never known, dropped.
      if (get_frame_timestamps_(display_, surface_, pending_.front().first, 1, &present_name,
                                &present) &&
          present == EGL_TIMESTAMP_PENDING_ANDROID) {
        break;  // in order: the later ones are pending too
      }
      if (present > 0) {
        shown.push_back({pending_.front().second,
                         std::chrono::steady_clock::time_point(std::chrono::nanoseconds(present)),
                         std::chrono::nanoseconds(interval)});
      }
      pending_.pop_front();
    }
    return shown;
  }

 private:
  // The current surface, with timestamps on; false where there is none or no extension.
  bool Current() {
    const EGLDisplay display = eglGetCurrentDisplay();
    const EGLSurface surface = eglGetCurrentSurface(EGL_DRAW);
    if (display == EGL_NO_DISPLAY || surface == EGL_NO_SURFACE) return false;
    if (display == display_ && surface == surface_) return enabled_;
    display_ = display;
    surface_ = surface;
    pending_.clear();
    enabled_ = false;
    const char* extensions = eglQueryString(display, EGL_EXTENSIONS);
    if (!extensions || !std::strstr(extensions, "EGL_ANDROID_get_frame_timestamps")) return false;
    get_next_frame_id_ = reinterpret_cast<PFNEGLGETNEXTFRAMEIDANDROIDPROC>(
        eglGetProcAddress("eglGetNextFrameIdANDROID"));
    get_frame_timestamps_ = reinterpret_cast<PFNEGLGETFRAMETIMESTAMPSANDROIDPROC>(
        eglGetProcAddress("eglGetFrameTimestampsANDROID"));
    get_compositor_timing_ = reinterpret_cast<PFNEGLGETCOMPOSITORTIMINGANDROIDPROC>(
        eglGetProcAddress("eglGetCompositorTimingANDROID"));
    if (!get_next_frame_id_ || !get_frame_timestamps_ || !get_compositor_timing_) return false;
    enabled_ = eglSurfaceAttrib(display, surface, EGL_TIMESTAMPS_ANDROID, EGL_TRUE) == EGL_TRUE;
    return enabled_;
  }

  EGLDisplay display_ = EGL_NO_DISPLAY;
  EGLSurface surface_ = EGL_NO_SURFACE;
  bool enabled_ = false;
  PFNEGLGETNEXTFRAMEIDANDROIDPROC get_next_frame_id_ = nullptr;
  PFNEGLGETFRAMETIMESTAMPSANDROIDPROC get_frame_timestamps_ = nullptr;
  PFNEGLGETCOMPOSITORTIMINGANDROIDPROC get_compositor_timing_ = nullptr;
  std::deque<std::pair<EGLuint64KHR, uint64_t>> pending_;  // EGL frame id, our id; oldest first
};

}  // namespace

std::unique_ptr<PresentationClock> CreatePresentationClock(const NativeWindow& window) {
  if (window.system != NativeWindow::kAndroid) return nullptr;
  return std::make_unique<AndroidPresentationClock>();
}

}  // namespace torchlight::platform
