// Tests for FrameSizeForVideoMode (which video modes keep the game's own table and which frame the
// others get) and VideoModeForAspect (the mode reported for an aspect, within 4:3 to 32:9).

#include "hooks/video_mode.h"

#include <cstdio>
#include <cstdlib>

using torchlight::hooks::FrameSize;
using torchlight::hooks::FrameSizeForVideoMode;
using torchlight::hooks::VideoModeForAspect;

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}
}  // namespace

int main() {
  Check(!FrameSizeForVideoMode(1280, 720), "16:9 keeps the table");
  Check(!FrameSizeForVideoMode(1920, 1080), "16:9 at another size keeps the table");
  Check(!FrameSizeForVideoMode(956, 720), "4:3 keeps the table");
  Check(!FrameSizeForVideoMode(640, 480), "exact 4:3 keeps the table");
  Check(!FrameSizeForVideoMode(0, 720), "empty mode");
  Check(FrameSizeForVideoMode(1280, 800) == FrameSize{1280, 800}, "16:10");
  Check(FrameSizeForVideoMode(1920, 1200) == FrameSize{1280, 800}, "16:10 at another size");
  Check(FrameSizeForVideoMode(1680, 720) == FrameSize{1680, 720}, "21:9 (1680x720)");
  Check(FrameSizeForVideoMode(2560, 1080) == FrameSize{1680, 720}, "21:9 (2560x1080)");
  Check(FrameSizeForVideoMode(3440, 1440) == FrameSize{1760, 720}, "43:18 (3440x1440)");
  Check(FrameSizeForVideoMode(2560, 720) == FrameSize{2560, 720}, "32:9");
  Check(FrameSizeForVideoMode(5120, 1440) == FrameSize{2560, 720}, "32:9 (5120x1440)");
  Check(FrameSizeForVideoMode(4095, 480) == FrameSize{2560, 720}, "wider than 32:9 is capped");
  Check(FrameSizeForVideoMode(5760, 1080) == FrameSize{2560, 720}, "three 16:9 screens: 32:9");
  for (uint32_t w = 1300; w < 4000; w += 37) {
    if (auto f = FrameSizeForVideoMode(w, 720)) Check(f->width % 80 == 0, "width in 80s");
  }

  // The frame a display aspect ends up with: the reported mode, then the guest's frame for it.
  auto frame = [](double aspect) {
    const FrameSize mode = VideoModeForAspect(aspect);
    return FrameSizeForVideoMode(mode.width, mode.height);
  };
  Check(VideoModeForAspect(16.0 / 9.0) == FrameSize{1280, 720}, "16:9 mode");
  Check(!frame(16.0 / 9.0), "16:9: the game's 16:9");
  Check(!frame(1366.0 / 768.0), "1366x768: the game's 16:9");
  Check(VideoModeForAspect(4.0 / 3.0) == FrameSize{956, 720}, "exact 4:3 is reported narrower");
  Check(956 * 3 < 720 * 4, "956x720 is not widescreen for the runtime");
  Check(!frame(4.0 / 3.0), "4:3: the game's own 4:3");
  Check(VideoModeForAspect(5.0 / 4.0) == FrameSize{956, 720}, "5:4 gives 4:3");
  Check(!frame(5.0 / 4.0), "5:4: the game's own 4:3");
  Check(frame(16.0 / 10.0) == FrameSize{1280, 800}, "16:10");
  Check(frame(21.0 / 9.0) == FrameSize{1680, 720}, "21:9");
  Check(frame(3440.0 / 1440.0) == FrameSize{1760, 720}, "3440x1440");
  Check(VideoModeForAspect(32.0 / 9.0) == FrameSize{2560, 720}, "32:9 mode");
  Check(frame(32.0 / 9.0) == FrameSize{2560, 720}, "32:9");
  Check(VideoModeForAspect(48.0 / 9.0) == FrameSize{2560, 720}, "three 16:9 screens give 32:9");
  Check(frame(48.0 / 9.0) == FrameSize{2560, 720}, "three 16:9 screens: 32:9 frame");
  Check(frame(10.0) == FrameSize{2560, 720}, "very wide: 32:9");
  Check(VideoModeForAspect(0) == FrameSize{1280, 720}, "unknown aspect: 16:9");
  Check(!frame(0), "unknown aspect: the game's 16:9");

  if (failures) return EXIT_FAILURE;
  std::puts("video_mode_test: ok");
  return EXIT_SUCCESS;
}
