// The game's adaptive vsync (guest_abi adaptive_vsync) under the host's fps cap: the cap is the
// guest's vblank rate, so at a cap of 30 the game's lastFPS sits at about 30, under its threshold
// of 31, and the game dropped the vsync every other second (47.7 fps measured at rest with the cap
// at 30). Under a host cap of 31 or less the presents always wait (the cap is never exceeded, a
// frame that misses the vblank waits for the next); over 31, and without a host cap, the game
// decides as it always did (a second under 31 frames lifts the vsync for the next one). Pure,
// tested in adaptive_vsync_test.

#pragma once

#include <cstdint>

namespace torchlight::hooks {

// The presentation interval to hand the device: `game_interval` as the game chose it, under the
// host's fps cap `fps_cap` (0: none).
uint32_t AdaptiveVsyncInterval(uint32_t game_interval, uint32_t fps_cap);

// The host's fps cap in force (0: none, or the vsync left to the command line): set by the
// settings (at start and from the video menu), read by the hook; any thread.
void SetAdaptiveVsyncCap(uint32_t fps_cap);
uint32_t AdaptiveVsyncCap();

}  // namespace torchlight::hooks
