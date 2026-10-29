// The game's own adaptive vsync: once a second, a frame listener of the game (sub_821FB108, its
// frameStarted) reads the render window's FrameStats::lastFPS and, under 31 frames a second, sets
// the D3D device's presentation interval to IMMEDIATE (presents stop waiting for the vblank),
// otherwise DEFAULT. On a 360 it keeps a game under 31 fps from being held to 20; with the host's
// fps cap at 30 (the guest's vblank at 30 Hz) lastFPS is always about 30, so the game runs
// unthrottled every other second (hooks/adaptive_vsync.h).

#pragma once

#include <cstdint>

namespace torchlight::guest_abi::adaptive_vsync {

// [confirmed] `bl 0x82768610` (the device's presentation interval setter, `stw r4,13872(r3)`
// @0x82768610), reached from both branches of `fcmpu cr6,f13,f0` / `blt` @0x821FB1C0: r4 the
// interval, f13 lastFPS (`lfs f13,0(r3)` of RenderTarget::getStatistics, the D3D9RenderWindow's
// vtable slot 18 @0x826575A8 `addi r3,r3,56`: Runic's vtable, slot 14 in OgreRenderTarget.h).
inline constexpr uint32_t kIntervalCall = 0x821FB1CC;
// [confirmed] The threshold: the float at 0x820AA754 (`lfs f0,1172(r8)`, r8 = 0x820AA2C0) is
// 31.0f (bytes 41 F8 00 00).
inline constexpr float kThresholdFps = 31.0f;
// [confirmed] r4 as the two branches set it: `lis r4,-32768` (D3DPRESENT_INTERVAL_IMMEDIATE) and
// `li r4,0` (D3DPRESENT_INTERVAL_DEFAULT).
inline constexpr uint32_t kIntervalImmediate = 0x80000000u;
inline constexpr uint32_t kIntervalDefault = 0;

}  // namespace torchlight::guest_abi::adaptive_vsync
