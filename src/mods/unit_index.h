// The game's unit index file (MEDIA/UNITDATA.RAW; guest_abi/unit_index.h, docs/mods.md section
// 7e): reading, writing and comparing it, and merging the player's mods' units into it the way PC
// Torchlight builds its table (a mod's unit file replaces the base one with the same path; the
// first mod in the search order, the lowest PRIORITY, wins). No guest, OGRE or platform types.
//
// Layout (little-endian), as the game's loader reads it: 4 groups (the folder under MEDIA/UNITS/:
// ITEMS, MONSTERS, PLAYERS, PROPS), each a u32 count and its entries. An entry: i64 UNIT_GUID;
// text NAME (upper case); text the definition's path; u8 CREATEAS == "EQUIPMENT"; u32 LEVEL,
// MINLEVEL, MAXLEVEL, RARITY, RARITY_HARDCORE; text UNITTYPE. A text is a u16 count of UTF-16
// units followed by them.

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace torchlight::mods {

enum class UnitGroup : uint32_t { kItems = 0, kMonsters = 1, kPlayers = 2, kProps = 3 };
inline constexpr size_t kUnitGroupCount = 4;

struct UnitEntry {
  int64_t guid = 0;
  std::u16string name;       // NAME, upper case
  std::u16string file;       // e.g. u"MEDIA/UNITS/ITEMS/GOLD/GOLD_100.DAT"
  bool equipment = false;    // CREATEAS == "EQUIPMENT"
  uint32_t level = 0;
  uint32_t min_level = 0;
  uint32_t max_level = 0;
  uint32_t rarity = 0;
  uint32_t rarity_hardcore = 0;
  std::u16string unit_type;  // UNITTYPE, as written

  bool operator==(const UnitEntry&) const = default;
};

struct UnitIndex {
  std::array<std::vector<UnitEntry>, kUnitGroupCount> groups;
  size_t size() const;
};

// The file's content; none (with `error`) unless every byte is consumed.
std::optional<UnitIndex> ParseUnitIndex(std::span<const uint8_t> bytes, std::string* error);
std::vector<uint8_t> WriteUnitIndex(const UnitIndex& index);

// The group of a definition's path (MEDIA/UNITS/<group>/..., any case, '/' or '\'); none outside.
std::optional<UnitGroup> GroupOfUnitFile(std::u16string_view path);
// Paths compared the way the game's files are: any case, '/' and '\' alike.
std::u16string NormalizeUnitFile(std::u16string_view path);

// Differences between two indexes, entry by entry (matched by GUID within each group, order
// ignored), as readable lines; at most `limit` lines plus a count of the rest. Empty: equivalent.
std::vector<std::string> CompareUnitIndexes(const UnitIndex& expected, const UnitIndex& actual,
                                            size_t limit = 50);

// `base` with the mods' units, `units` in increasing precedence (the last wins; see
// unit_cache.h UnitPathsByPriority). A unit whose file is also a base unit's (same path) takes that
// entry's place; one whose GUID another entry has replaces it; any other goes to the end of its
// group. Units outside MEDIA/UNITS/<group>/ are
// left out and listed in `skipped`.
UnitIndex MergeUnitIndex(const UnitIndex& base, const std::vector<UnitEntry>& units,
                         std::vector<std::u16string>* skipped);

// How many entries the game keeps from `index`: it drops an entry without a NAME and files the
// rest by GUID, an entry whose GUID is already there replacing it (guest_abi unit_index.h, index),
// so this is the distinct GUIDs of the entries with a name.
size_t LoadedUnitCount(const UnitIndex& index);

// What the game holds after loading a file with `expected` unique GUIDs, `loaded` being the size
// of its GUID map (guest_abi unit_index.h index::kGuidMapSize).
enum class LoadedIndex {
  kComplete,    // every unit
  kEmpty,       // nothing: the file was not read (the loader skips everything, with no error)
  kIncomplete,  // part of it
  kMore,        // more than the file holds: something else was loaded too
};
LoadedIndex CheckLoadedIndex(size_t loaded, size_t expected);

}  // namespace torchlight::mods
