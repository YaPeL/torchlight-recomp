// The display's refresh as a clock, where the window system does not pace the GL presents itself.
//
// On macOS the GL swap interval OGRE sets (NSOpenGLCPSwapInterval) does not hold presents to the
// display: AppKit makes every view layer-backed (apps built with the macOS 10.14 SDK or later), and
// a GL context on a layer-backed view is composited by Core Animation, which takes a flush whenever
// it comes. With vsync on and a frame rate cap of 120 the game presented 112-120 frames a second
// on a 60 Hz display, and at the automatic cap's 60 the presents followed the cap's timer instead
// of the display: a frame now and then shown twice and the next one never, which reads as judder.
// There the live mode waits for the display's next refresh before each present itself (a
// CVDisplayLink on the window's display), and leaves the swap interval off so the two never wait
// together. Elsewhere the swap interval holds (null).

#pragma once

#include <chrono>
#include <memory>

#include "platform/platform.h"

namespace torchlight::platform {

class RefreshClock {
 public:
  virtual ~RefreshClock() = default;
  // Backend thread: waits for the display's next refresh; false when none came within `timeout`
  // (the display asleep, the link stopped), so a present never waits longer than that.
  virtual bool WaitNextRefresh(std::chrono::milliseconds timeout) = 0;
};

// The backend's window: null where the window system paces GL presents by the swap interval.
std::unique_ptr<RefreshClock> CreateRefreshClock(const NativeWindow& window);

}  // namespace torchlight::platform
