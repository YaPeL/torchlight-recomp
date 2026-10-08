// The guest's frame size for other aspect ratios (video_mode_hooks.cpp).

#pragma once

namespace torchlight::hooks {

// After the guest image is loaded, before it runs: renames the game's 16:9 video mode for the frame
// of the configured video mode, which lets the mode hooks resize it. Does nothing for 16:9 or 4:3.
void InstallVideoMode();
// Whether the guest draws a frame wider than 16:9 (InstallVideoMode succeeded for one).
bool WiderThan16x9();
// The viewport whose draws the native backend limits to the centred 16:9 strip (SetSceneClip): the
// scene viewport while the game is in its front end on a wider frame, else 0.
uint32_t SceneClipViewport(const uint8_t* base);
// The scene's viewport as CreateViewports made it (0 before), in a level or not.
uint32_t SceneViewport();
// CreateViewports made the views (`scene_viewport`: the scene's); used by its hook.
void ViewsCreated(uint32_t scene_viewport);

}  // namespace torchlight::hooks
