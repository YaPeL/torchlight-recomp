// OGRE built as static libraries (Android): the GLES2 render system (GLES 3 where the context has
// it) and the STBI codec are linked in and registered in code (ogre_plugins.h). The backend's
// TL_RENDER_SYSTEM_GL3PLUS is OGRE's GL family: GLES2 is the one a static build has.

#include "backend/ogre_plugins.h"

#include <Ogre.h>
#include <OgreGLES2Plugin.h>
#include <OgreSTBICodec.h>

namespace torchlight::backend {
namespace {

class StaticOgrePlugins final : public OgrePlugins {
 public:
  explicit StaticOgrePlugins(Ogre::Root& root) {
    root.installPlugin(&render_system_);
    // Image files the guest loads are DDS (built into OgreMain); a few are PNG.
    Ogre::STBIImageCodec::startup();
  }
  ~StaticOgrePlugins() override { Ogre::STBIImageCodec::shutdown(); }

 private:
  Ogre::GLES2Plugin render_system_;  // the root uninstalls it; it must outlive the root
};

}  // namespace

std::unique_ptr<OgrePlugins> OgrePlugins::Load(Ogre::Root& root, tl_render_system render_system,
                                               const torchlight::platform::NativeWindow*) {
  if (render_system != TL_RENDER_SYSTEM_GL3PLUS) {
    OGRE_EXCEPT(Ogre::Exception::ERR_FILE_NOT_FOUND,
                "this build of OGRE is static and has only the GLES2 render system",
                "OgrePlugins::Load");
  }
  return std::make_unique<StaticOgrePlugins>(root);
}

}  // namespace torchlight::backend
