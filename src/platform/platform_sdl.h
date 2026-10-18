// Internal to the platform module: what each platform file provides to platform_sdl.cpp.

#pragma once

#include <SDL3/SDL_video.h>

#include "platform/platform.h"

namespace torchlight::platform {

// The native handles of the game window (SDL's) for the backend. False when SDL has none for it.
bool NativeGameWindow(SDL_Window* game, NativeWindow& window);
// Before a message box or a file picker of the first start, which has no window of its own yet:
// macOS brings the application to the front (started from a terminal, it stays behind the
// terminal and its dialogs with it); Linux and Windows show them in front already.
void BringToFront();
// The dots per inch SDL's content scale 1.0 stands for here: Android's 160 (its density's
// baseline), 96 on Windows and Linux, 72 on macOS (points).
float ContentScaleDpi();

}  // namespace torchlight::platform
