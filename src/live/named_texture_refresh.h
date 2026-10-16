#pragma once

#include <string>
#include <vector>

namespace torchlight::live {
// Textures the guest loaded by name whose file changed (the resolver now gives other bytes).
// Any thread. Each name is re-resolved through mods::CurrentTextureResolver() and every guest
// texture created from it gets the new content from the next frame on (the same guest texture,
// a new content version; captures and session recordings keep the new bytes). Names as the guest
// gives them or as WritePromptTextures returns them: compared after TextureNameKey.
void RefreshNamedTextures(std::vector<std::string> names);
}  // namespace torchlight::live
