// Finding a mod's file the way Windows would (docs/mods.md, section 7f). The game's per-mod file
// map (guest_abi/mods.h kModFileLookup) keys each file by its path inside the mod as listed on the
// player's disk ("media/units/items/x/SWORD.DAT", '/' between folders) and looks a request up by
// exact comparison, so a mod made on Windows (case-insensitive) is missed whenever the game asks
// with other letter case, e.g. MEDIA/UNITS/... from the unit index. The host keeps, per enabled mod,
// the files under their normalized path, and gives the game the exact spelling to ask again with.
// No guest, OGRE or platform types.

#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mods/mod_list.h"

namespace torchlight::mods {

// Upper case (ASCII), '\' as '/', no leading "./" or '/'.
std::u16string NormalizeModPath(std::u16string_view path);

class ModFileTable {
 public:
  // The enabled mods' files, in the plan's order (the order the game searches them).
  static ModFileTable Scan(const std::filesystem::path& mods_folder, const ModPlan& plan);
  // For tests: (mod, path inside it as on disk) pairs, in search order.
  void Add(size_t mod, std::u16string_view path_on_disk);

  // The spelling the game's map has for `request` in the first mod (search order) that has it
  // under another letter case or separators; none when no mod has it at all.
  std::optional<std::u16string> Spelling(std::u16string_view request) const;
  size_t size() const { return files_.size(); }

 private:
  // normalized path -> (mod, spelling) for each mod that has it, in search order.
  std::map<std::u16string, std::vector<std::pair<size_t, std::u16string>>> files_;
};

}  // namespace torchlight::mods
