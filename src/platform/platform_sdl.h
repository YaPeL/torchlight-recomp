// Internal to the platform module: what each platform file provides to platform_sdl.cpp.

#pragma once

#include <SDL3/SDL_video.h>

#include "platform/platform.h"

namespace torchlight::platform {

// The native handles of the game window (SDL's) for the backend. False when SDL has none for it.
bool NativeGameWindow(SDL_Window* game, NativeWindow& window);

}  // namespace torchlight::platform
