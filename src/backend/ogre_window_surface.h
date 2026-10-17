// OGRE's window letting go of the surface it draws in and taking a new one, where the window
// system takes it away (Android: platform::ServeSurfaceChanges). OGRE declares these on Android
// only, so CMake compiles ogre_window_surface_android.cpp there and ogre_window_surface_none.cpp
// elsewhere, where they are never called. Inside backend/ only.

#pragma once

#include <cstdint>

namespace Ogre {
class RenderWindow;
}

namespace torchlight::backend {

// The EGL surface goes; the context, with every texture, buffer and program, stays (the window is
// made with preserveContext).
void ReleaseWindowSurface(Ogre::RenderWindow& window);
// A new surface on `native_window` (an ANativeWindow*).
void RestoreWindowSurface(Ogre::RenderWindow& window, uint64_t native_window);

}  // namespace torchlight::backend
