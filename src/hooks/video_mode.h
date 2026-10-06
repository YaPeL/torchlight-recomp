// The guest frame size for a display aspect ratio other than the game's own (16:9 and 4:3).

#pragma once

#include <cstdint>
#include <optional>

namespace torchlight::hooks {

struct FrameSize {
  uint32_t width = 0, height = 0;
  bool operator==(const FrameSize&) const = default;
};

// The frame for a video mode of `width` x `height`, or nothing when the game's own table covers
// its aspect (16:9 gives 1280x720, 4:3 or narrower 960x720). 16:10 gives 1280x800; any other
// aspect keeps 720 lines and a width in multiples of 80 (the guest D3D still sizes surfaces in
// Xenos EDRAM tiles, 80 pixels wide), at most 2560 (32:9, the widest validated).
std::optional<FrameSize> FrameSizeForVideoMode(uint32_t width, uint32_t height);

// The video mode for the runtime to report (its video_mode_width/height cvars) so the guest draws
// a frame of display aspect `aspect` (width / height), kept within the validated range: 4:3 or
// narrower gives 956x720, since the runtime reports a mode as widescreen when width * 3 >= height
// * 4, so an exact 4:3 would make the game pick its 16:9 mode; 32:9 or wider gives 2560x720; any
// other aspect, 720 lines at that aspect. Not a positive aspect: 1280x720.
FrameSize VideoModeForAspect(double aspect);

}  // namespace torchlight::hooks
