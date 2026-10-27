// The wardrobe build's blend loop on the host (hooks/wardrobe_blend.h): the midasm hook
// WardrobeBlendLoop (torchlight_manifest.toml, guest_abi wardrobe::blend kHookAddress) runs one item
// image's whole loop and skips the recompiled one. --native_wardrobe_blend=false, or
// --native_pixel_util=false (the pack/unpack code is shared), keeps the recompiled loop.

#pragma once

#include <cstdint>

namespace torchlight::hooks {

// --native_wardrobe_blend and --native_pixel_util both on.
bool NativeWardrobeBlendEnabled();

// Diagnostics builds (TORCHLIGHT_DIAGNOSTICS) with --diag_wardrobe_blend_check: each item image's
// loop is computed on the host into a copy and the recompiled loop runs; each of its packs
// (returning to guest_abi wardrobe::blend::kPackReturn, after the pack wrote dest) is compared with
// the host's pixel, and its stack temporaries after the last one. Called by the pack hook
// (hooks/pixel_util_hooks.cpp). Nothing in other builds.
void WardrobeBlendCheckPacked(uint8_t* base, uint32_t dest);

// Diagnostics builds: logs the check's counts since the last call (after each wardrobe build).
void LogWardrobeBlendCheck();

}  // namespace torchlight::hooks
