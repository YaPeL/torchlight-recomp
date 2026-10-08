// The cached unit index for the player's mods (docs/mods.md, section 7e). When mods bring unit
// definitions (files under media/units/), the host merges them into the game's index
// (unit_index.h) and keeps the result in the user's folder, <DataDir>/cache/unitdata/<key>.RAW,
// never in the game's data. The key changes when any of these does: the mods' unit files (which
// mod, which file, what content, in priority order), the base game (its pak's central directory)
// or this code (kUnitIndexVersion). Files are written whole and renamed into place, and a cached
// file that does not read back as an index is thrown away and built again. No guest, OGRE or
// platform types.

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "mods/mod_list.h"
#include "mods/unit_index.h"

namespace torchlight::mods {

// Bumped whenever what goes into a cached index changes (how entries are read or merged); 3: indexes
// built with units left out were cached under the key (IncompleteUnitIndexName).
inline constexpr uint32_t kUnitIndexVersion = 3;

// One unit definition a mod brings: the mod's folder, the path inside it as the game names it
// ("MEDIA/UNITS/..."; upper case, '/') and a digest of its bytes.
struct ModUnitFile {
  std::string mod_folder;
  std::u16string game_path;
  uint64_t digest = 0;
  std::filesystem::path file;  // on disk
};

// The enabled mods' unit definitions (*.dat under media/units/, any case), in the plan's
// registration order: the game's search order, where the first mod that has a file wins (the
// lowest PRIORITY, as on PC; guest_abi/mods.h kModFileLookup). Disabled mods are left out.
std::vector<ModUnitFile> ScanModUnitFiles(const std::filesystem::path& mods_folder, const ModPlan& plan);

// The distinct game paths the mods define units at, the ones whose winning mod comes first in the
// search order last (so merging in this order lets that mod's unit win a GUID clash).
std::vector<std::u16string> UnitPathsByPriority(const std::vector<ModUnitFile>& files);

// The UNIT_GUID each file sets (text definitions, the format mods ship), or none (with `why`)
// when any file cannot be read or sets none: then the mods' units are not all known.
std::optional<std::vector<int64_t>> ModUnitGuids(const std::vector<ModUnitFile>& files, std::string* why);

// The base index from the game's pak (media/UNITDATA.RAW), or none (with `error`).
std::optional<UnitIndex> ReadPakUnitIndex(const std::filesystem::path& pak, std::string* error);

// What identifies the base game: a digest of the pak's central directory (each entry's name, size
// and CRC-32), cheap to read; none when the file is not a zip.
std::optional<uint64_t> PakIdentity(const std::filesystem::path& pak);

// The cache key (hex) for the mods' unit files, the base and kUnitIndexVersion.
std::string UnitCacheKey(const std::vector<ModUnitFile>& files, uint64_t base_identity);

// <folder>/<key>.RAW when it exists and reads back as an index; a file that does not is removed
// (`log` says why) and none is returned, so the caller builds it again.
std::optional<std::filesystem::path> FindCachedUnitIndex(const std::filesystem::path& folder,
                                                         const std::string& key, std::string* log);

// The name an index is stored under instead of its key when some mod unit could not be read into
// it (the game could not load the definition). FindCachedUnitIndex never finds it under the key, so
// the next start builds the index again rather than keeping the units out for good; the next
// StoreCachedUnitIndex removes it.
std::string IncompleteUnitIndexName(const std::string& key);

// Writes <folder>/<key>.RAW whole (a temporary file, flushed, renamed into place) and removes the
// other cached indexes. False (with `error`) when it cannot.
bool StoreCachedUnitIndex(const std::filesystem::path& folder, const std::string& key,
                          const UnitIndex& index, std::string* error);

}  // namespace torchlight::mods
