// Android: OGRE's Android EGL window (ogre_window_surface.h).

#include "backend/ogre_window_surface.h"

#include <Ogre.h>

namespace torchlight::backend {

void ReleaseWindowSurface(Ogre::RenderWindow& window) { window._notifySurfaceDestroyed(); }

void RestoreWindowSurface(Ogre::RenderWindow& window, uint64_t native_window) {
  window._notifySurfaceCreated(reinterpret_cast<void*>(uintptr_t(native_window)));
}

}  // namespace torchlight::backend
