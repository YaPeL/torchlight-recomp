// Which file the guest opened for a named texture, for the mods' texture check (docs/mods.md 7j;
// mods_install.cpp, behind TORCHLIGHT_MODS_DIAGNOSTICS): the game's ResourceGroupManager::openResource
// is followed, and the archive that served the file (a folder on one of our devices, or the pak's
// zip) is the last one opened inside it. Without the guest: one recorder per thread.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace torchlight::game_menu {

// Whether a resource name is a texture's, by its extension (any case).
bool IsTextureName(std::string_view name);

struct TextureOpen {
  std::string name;       // as the game asked for it
  std::string container;  // the archive's name ("tlmod003:\", "tlrepl:\", the pak's zip)
  std::string path;       // the file inside it, as the archive was asked
};

class TextureOpenRecorder {
 public:
  // openResource called, with the guest's stack pointer (nested calls happen: a search in the other
  // groups calls it again, deeper in the stack). A call at or above the outermost one's stack means
  // that one never returned through the hook (the game threw past it): it starts over.
  void Begin(std::string_view name, uint32_t stack);
  // An archive opened a file during the outermost call.
  void ArchiveOpened(std::string_view container, std::string_view path);
  // openResource returned: for the outermost call of a texture name that an archive opened, what
  // was opened (the last archive wins).
  std::optional<TextureOpen> End();

 private:
  int depth_ = 0;
  uint32_t outer_stack_ = 0;
  std::string name_;
  std::optional<std::pair<std::string, std::string>> opened_;
};

}  // namespace torchlight::game_menu
