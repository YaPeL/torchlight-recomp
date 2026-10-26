// The decoded item texture images of the equipment texture build (hooks/image_cache.h); the hook is
// in image_cache_hooks.cpp. Every mode.

#pragma once

namespace torchlight::hooks {

// Before the guest runs: the cache's bound from --wardrobe_image_cache_mb (or the platform's
// default), and the log line saying it.
void InstallWardrobeImageCache();

}  // namespace torchlight::hooks
