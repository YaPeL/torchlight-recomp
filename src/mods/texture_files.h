// Which file a named texture comes from, the way the guest's OGRE resolves it with the player's
// mods (docs/mods.md 7j), for the host: the live frontend reads that file, and a capture keeps its
// bytes when it is not the pak's (replay has no mods).
//
// A texture is opened in its material's resource group, which the guest records in the texture
// (Resource::mGroup, guest_abi ogre_layout.h resource::kGroup):
// - In a mod's group: that mod's folder first. Not found there, OGRE looks in the other groups in
//   the order they were made: the game's (the replacements folder, then the pak), then the mods in
//   activation order.
// - In any other group (the game's): the replacements folder (added last to the pak's groups, it
//   wins their index), then the pak, then the mods in activation order.
// Names are the guest's ("MEDIA/LEVELSETS/..."); files in the folders match ignoring case, with
// '/' and '\' alike.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace torchlight::mods {

struct TextureLocations {
  std::filesystem::path replacements;  // empty: no replacements folder in use
  // Each active mod's resource group and folder, in activation order.
  std::vector<std::pair<std::string, std::filesystem::path>> mods;
};

enum class TextureSource { kNone, kReplacements, kMod, kPak };

struct TextureFile {
  TextureSource source = TextureSource::kNone;
  std::filesystem::path path;  // kReplacements and kMod: the host file
};

// The key the folders and the pak are matched by: upper case, '/' between folders, no leading
// separator.
std::string TextureNameKey(std::string_view name);

class TextureResolver {
 public:
  // Lists every folder now (their files are fixed while the game runs).
  explicit TextureResolver(const TextureLocations& locations);

  // `pak_has` tells whether the pak holds the name (any case, either separator).
  TextureFile Find(std::string_view name, std::string_view group,
                   const std::function<bool(std::string_view)>& pak_has) const;

  bool empty() const { return !replacements_ && mods_.empty(); }

 private:
  struct Folder {
    std::unordered_map<std::string, std::filesystem::path> files;  // TextureNameKey -> file
    std::optional<std::filesystem::path> Find(const std::string& key) const;
  };
  static Folder List(const std::filesystem::path& root);
  std::optional<Folder> replacements_;
  std::vector<std::pair<std::string, Folder>> mods_;
};

// The session's resolver: set once before the guest loads textures (the mods' registration), read
// from any thread. Null without mods or replacements.
void SetTextureLocations(const TextureLocations& locations);
std::shared_ptr<const TextureResolver> CurrentTextureResolver();

// The pak's entry names (TextureNameKey), published by whoever opens the pak (the live mode), for
// a resolver used where the pak is not open (the capture). Empty until set: a pak that did not
// open counts as empty, so a texture would then be taken from a mod's folder when one has it.
void SetPakNames(const std::vector<std::string>& names);
bool PakHas(std::string_view name);

}  // namespace torchlight::mods
