// How the backend gets OGRE's render system and its image codec. Where OGRE is built as shared
// libraries (the desktop installs) they are plugins loaded from disk, from the platform's plugin
// directory; where it is built static (Android: OGRE builds only static libraries there) they are
// linked in and registered in code. CMake compiles one of ogre_plugins_dynamic.cpp and
// ogre_plugins_static.cpp, by the OGRE install found (OGRE_STATIC), not by platform. Inside
// backend/ only.

#pragma once

#include <memory>

#include "backend/backend_api.h"

namespace Ogre {
class Root;
}

namespace torchlight::platform {
struct NativeWindow;
}

namespace torchlight::backend {

class OgrePlugins {
 public:
  // The render system `render_system` and the image codec, made available to `root`; `parent` is
  // the window the backend draws inside, if any (platform::OgreRenderSystemDir). A render system
  // this build does not have throws Ogre::Exception, as a plugin file that is not there does.
  static std::unique_ptr<OgrePlugins> Load(Ogre::Root& root, tl_render_system render_system,
                                           const torchlight::platform::NativeWindow* parent);
  // Destroyed after the root, which uninstalls the plugins it was given.
  virtual ~OgrePlugins() = default;
};

}  // namespace torchlight::backend
