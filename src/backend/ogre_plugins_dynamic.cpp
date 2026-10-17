// OGRE built as shared libraries: the render system and the codec are plugin files in the
// platform's plugin directory (ogre_plugins.h).

#include "backend/ogre_plugins.h"

#include <string>

#include <Ogre.h>

#include "platform/platform.h"

namespace torchlight::backend {

std::unique_ptr<OgrePlugins> OgrePlugins::Load(Ogre::Root& root, tl_render_system render_system,
                                               const torchlight::platform::NativeWindow* parent) {
  std::string plugins = torchlight::platform::OgrePluginDir();
  if (!plugins.empty() && plugins.back() == '/') plugins.pop_back();
  if (render_system == TL_RENDER_SYSTEM_D3D11) {
    root.loadPlugin(plugins + "/RenderSystem_Direct3D11");
  } else {
    root.loadPlugin(torchlight::platform::OgreRenderSystemDir(plugins, parent) +
                    "/RenderSystem_GL3Plus");
  }
  // Image files the guest loads are DDS (built into OgreMain); a few are PNG.
  root.loadPlugin(plugins + "/Codec_STBI");
  // The root unloads the plugin files it loaded; nothing else to keep.
  return std::make_unique<OgrePlugins>();
}

}  // namespace torchlight::backend
