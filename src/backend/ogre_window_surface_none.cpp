// Where the window system never takes the surface away (ogre_window_surface.h): never called.

#include "backend/ogre_window_surface.h"

namespace torchlight::backend {

void ReleaseWindowSurface(Ogre::RenderWindow&) {}

void RestoreWindowSurface(Ogre::RenderWindow&, uint64_t) {}

}  // namespace torchlight::backend
