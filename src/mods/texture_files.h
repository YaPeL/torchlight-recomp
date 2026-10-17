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
//   wins their index), then the pad prompts' folder, then the pak, then the mods in activation
//   order.
// Names are the guest's ("MEDIA/LEVELSETS/..."); files in the folders match ignoring case, with
// '/' and '\' alike. A name that starts with a device ("TLMOD002:/MEDIA/...", "TLREPL:/...") is
// absolute: the guest's data loader gives a mod's file by its full path (a mod's TEXTURE_OVERRIDE,
// docs/mods.md 7j), and OGRE opens it only in the folder that device is; so does the host.

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
  // Each active mod, in activation order.
  struct Mod {
    std::string group;               // its resource group
    std::filesystem::path folder;    // its folder on the host
    std::string device;              // the guest's name for it ("tlmod003:"), for the check
  };
  std::vector<Mod> mods;
  // Files the app generates for the guest (the merged icon atlases, mods/icon_atlas.h): a folder
  // mounted as `generated_device` ("tlatlas:"), whose names reach the host with that device first.
  std::filesystem::path generated;  // empty: none
  std::string generated_device;
  // The pad prompts' textures (mods/pad_prompts.h): in the game's groups after the replacements
  // folder and before the pak, as files of the pak.
  std::filesystem::path prompts;  // empty: none (the Xbox style)
};

enum class TextureSource { kNone, kReplacements, kMod, kPak, kGenerated };

// Whether the file is one of the host's folders (a mod's, the replacements', a generated one), not
// the pak's: the live frontend reads it, and captures and session recordings keep its bytes, since
// replay has none of those folders.
inline bool FromHostFile(TextureSource source) {
  return source == TextureSource::kMod || source == TextureSource::kReplacements ||
         source == TextureSource::kGenerated;
}

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

  bool empty() const { return !replacements_ && mods_.empty() && !generated_ && !prompts_; }

 private:
  struct Folder {
    std::unordered_map<std::string, std::filesystem::path> files;  // TextureNameKey -> file
    std::optional<std::filesystem::path> Find(const std::string& key) const;
  };
  static Folder List(const std::filesystem::path& root);
  std::optional<Folder> replacements_;
  std::optional<Folder> generated_;
  std::optional<Folder> prompts_;
  std::string generated_device_;  // TextureNameKey'd ("TLATLAS:")
  std::vector<std::pair<std::string, Folder>> mods_;
  std::vector<std::string> devices_;  // mods_'s devices, TextureNameKey'd ("TLMOD002:")
};

// The session's resolver: set once before the guest loads textures (the mods' registration), read
// from any thread. Null without mods, replacements or pad prompts.
void SetTextureLocations(const TextureLocations& locations);
std::shared_ptr<const TextureResolver> CurrentTextureResolver();

// The pak's entry names (TextureNameKey), published by whoever opens the pak (the live mode), for
// a resolver used where the pak is not open (the capture). Empty until set: a pak that did not
// open counts as empty, so a texture would then be taken from a mod's folder when one has it.
void SetPakNames(const std::vector<std::string>& names);
bool PakHas(std::string_view name);

}  // namespace torchlight::mods
