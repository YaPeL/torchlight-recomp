// The game client's projector textures every other frame (projector_textures.h).

#pragma once

namespace torchlight::hooks {

// Before the guest runs: reads --projector_textures_every_other_frame and logs it.
void InstallProjectorTextures();

}  // namespace torchlight::hooks
