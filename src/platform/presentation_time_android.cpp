// Presentation times on Android (presentation_time.h): EGL_ANDROID_get_frame_timestamps on the
// EGL surface OGRE draws in. The backend thread holds that surface's context, so the display and
// surface are the current ones (OGRE's window gives no surface handle); the surface changes when
// the activity gives a new one back, and the frames of the old one are dropped. A new surface can
// reuse the old one's handle, so timestamps are checked on every frame, not only on a new handle.
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
    if (!get_next_frame_id_(display_, surface_, &frame)) {
      lost_ = true;
      return;
    }
    // A surface that never shows (the activity in the background): no unbounded growth.
    if (pending_.size() >= 64) {
      pending_.pop_front();
      lost_after_ = true;
    }
    pending_.push_back({frame, id, lost_});
    lost_ = false;
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
      const Pending& p = pending_.front();
      EGLnsecsANDROID present = EGL_TIMESTAMP_INVALID_ANDROID;
      // Fails for a frame older than the surface's history (a few frames): never known. A frame
      // still pending holds back the later ones, so a frame the compositor skipped delays the
      // results until it leaves that history.
      if (get_frame_timestamps_(display_, surface_, p.frame, 1, &present_name, &present) &&
          present == EGL_TIMESTAMP_PENDING_ANDROID) {
        break;
      }
      if (present > 0) {
        const std::chrono::steady_clock::time_point time{std::chrono::nanoseconds(present)};
        shown.push_back(
            {p.id, time, std::chrono::nanoseconds(interval), p.after_loss || lost_after_});
        lost_after_ = false;
      } else {
        lost_after_ = true;
      }
      pending_.pop_front();
    }
    return shown;
  }

 private:
  struct Pending {
    EGLuint64KHR frame;  // EGL's frame id
    uint64_t id;         // ours
    bool after_loss;     // a present before it went unmeasured
  };

  // The current surface, with timestamps on; false where there is none, or the device cannot tell
  // when frames reach the display.
  bool Current() {
    const EGLDisplay display = eglGetCurrentDisplay();
    const EGLSurface surface = eglGetCurrentSurface(EGL_DRAW);
    if (display == EGL_NO_DISPLAY || surface == EGL_NO_SURFACE) return false;
    if (display == display_ && surface == surface_) {
      if (!enabled_) return false;
      EGLint on = EGL_FALSE;
      if (eglQuerySurface(display, surface, EGL_TIMESTAMPS_ANDROID, &on) && on) return true;
      // A new surface with the old handle: its frames start over.
    }
    display_ = display;
    surface_ = surface;
    pending_.clear();
    lost_ = true;
    enabled_ = Enable();
    return enabled_;
  }

  bool Enable() {
    const char* extensions = eglQueryString(display_, EGL_EXTENSIONS);
    if (!extensions || !std::strstr(extensions, "EGL_ANDROID_get_frame_timestamps")) return false;
    auto proc = [](const char* name) { return eglGetProcAddress(name); };
    get_next_frame_id_ =
        reinterpret_cast<PFNEGLGETNEXTFRAMEIDANDROIDPROC>(proc("eglGetNextFrameIdANDROID"));
    get_frame_timestamps_ =
        reinterpret_cast<PFNEGLGETFRAMETIMESTAMPSANDROIDPROC>(proc("eglGetFrameTimestampsANDROID"));
    get_compositor_timing_ = reinterpret_cast<PFNEGLGETCOMPOSITORTIMINGANDROIDPROC>(
        proc("eglGetCompositorTimingANDROID"));
    auto timestamp_supported = reinterpret_cast<PFNEGLGETFRAMETIMESTAMPSUPPORTEDANDROIDPROC>(
        proc("eglGetFrameTimestampSupportedANDROID"));
    auto timing_supported = reinterpret_cast<PFNEGLGETCOMPOSITORTIMINGSUPPORTEDANDROIDPROC>(
        proc("eglGetCompositorTimingSupportedANDROID"));
    if (!get_next_frame_id_ || !get_frame_timestamps_ || !get_compositor_timing_ ||
        !timestamp_supported || !timing_supported) {
      return false;
    }
    // Without the display's present fences there is no present time (the interval is optional).
    if (!timestamp_supported(display_, surface_, EGL_DISPLAY_PRESENT_TIME_ANDROID)) return false;
    if (!timing_supported(display_, surface_, EGL_COMPOSITE_INTERVAL_ANDROID)) {
      get_compositor_timing_ = [](EGLDisplay, EGLSurface, EGLint, const EGLint*,
                                  EGLnsecsANDROID*) -> EGLBoolean { return EGL_FALSE; };
    }
    return eglSurfaceAttrib(display_, surface_, EGL_TIMESTAMPS_ANDROID, EGL_TRUE) == EGL_TRUE;
  }

  EGLDisplay display_ = EGL_NO_DISPLAY;
  EGLSurface surface_ = EGL_NO_SURFACE;
  bool enabled_ = false;
  bool lost_ = false;        // a present went unmeasured before the next one asked for
  bool lost_after_ = false;  // a frame asked for was never known, since the last one shown
  PFNEGLGETNEXTFRAMEIDANDROIDPROC get_next_frame_id_ = nullptr;
  PFNEGLGETFRAMETIMESTAMPSANDROIDPROC get_frame_timestamps_ = nullptr;
  PFNEGLGETCOMPOSITORTIMINGANDROIDPROC get_compositor_timing_ = nullptr;
  std::deque<Pending> pending_;  // oldest first
};

}  // namespace

std::unique_ptr<PresentationClock> CreatePresentationClock(const NativeWindow& window) {
  if (window.system != NativeWindow::kAndroid) return nullptr;
  return std::make_unique<AndroidPresentationClock>();
}

}  // namespace torchlight::platform
