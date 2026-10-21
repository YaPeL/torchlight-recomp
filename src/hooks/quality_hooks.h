// The graphics quality level (quality.h) in the game's settings.

#pragma once

namespace torchlight::hooks {

// Before the guest runs: reads --quality and logs it.
void InstallQuality();

}  // namespace torchlight::hooks
