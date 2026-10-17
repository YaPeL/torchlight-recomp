// The display's refresh on macOS (display_refresh.h): a CVDisplayLink on the display that shows the
// backend's window, whose callback (on Core Video's own thread) counts refreshes and wakes the
// backend thread waiting for the next one.

#include "platform/display_refresh.h"

#import <AppKit/AppKit.h>
#import <CoreVideo/CoreVideo.h>

#include <condition_variable>
#include <cstdint>
#include <mutex>

#include <rex/logging.h>

// CVDisplayLink is deprecated since macOS 15 in favour of NSView's displayLink (macOS 14 and
// later); the deployment target is 13.3.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

namespace torchlight::platform {

namespace {

class CocoaRefreshClock : public RefreshClock {
 public:
  ~CocoaRefreshClock() override {
    if (link_) {
      CVDisplayLinkStop(link_);
      CVDisplayLinkRelease(link_);
    }
  }

  bool Start(CGDirectDisplayID display) {
    if (CVDisplayLinkCreateWithCGDisplay(display, &link_) != kCVReturnSuccess || !link_) return false;
    CVDisplayLinkSetOutputCallback(link_, &CocoaRefreshClock::OnRefresh, this);
    return CVDisplayLinkStart(link_) == kCVReturnSuccess;
  }

  bool WaitNextRefresh(std::chrono::milliseconds timeout) override {
    std::unique_lock lock(mutex_);
    const uint64_t seen = refreshes_;
    return changed_.wait_for(lock, timeout, [&] { return refreshes_ != seen; });
  }

 private:
  static CVReturn OnRefresh(CVDisplayLinkRef, const CVTimeStamp*, const CVTimeStamp*, CVOptionFlags,
                            CVOptionFlags*, void* context) {
    auto* self = static_cast<CocoaRefreshClock*>(context);
    {
      std::lock_guard lock(self->mutex_);
      ++self->refreshes_;
    }
    self->changed_.notify_all();
    return kCVReturnSuccess;
  }

  CVDisplayLinkRef link_ = nullptr;
  std::mutex mutex_;
  std::condition_variable changed_;
  uint64_t refreshes_ = 0;  // under mutex_
};

}  // namespace

std::unique_ptr<RefreshClock> CreateRefreshClock(const NativeWindow& window) {
  if (window.system != NativeWindow::kCocoa || !window.window) return nullptr;
  // The window's display, read on the window system's thread (AppKit), the main display without
  // one. A window moved to another display keeps this one's clock (the game runs fullscreen).
  CGDirectDisplayID display = CGMainDisplayID();
  bool layer_backed = false;
  NSView* view = (NSView*)uintptr_t(window.window);
  RunOnWindowThread([&] {
    layer_backed = [view layer] != nil;
    NSScreen* screen = [[view window] screen];
    NSNumber* number = [[screen deviceDescription] objectForKey:@"NSScreenNumber"];
    if (number) display = CGDirectDisplayID([number unsignedIntValue]);
  });
  auto clock = std::make_unique<CocoaRefreshClock>();
  if (!clock->Start(display)) {
    REXLOG_WARN("display refresh: no display link for display {}; presents follow the swap interval",
                uint32_t(display));
    return nullptr;
  }
  REXLOG_INFO("display refresh: presents paced by display {}'s refresh (CVDisplayLink; the view is{} "
              "layer-backed)",
              uint32_t(display), layer_backed ? "" : " not");
  return clock;
}

}  // namespace torchlight::platform

#pragma clang diagnostic pop
