// Saved characters and stashes holding units the game cannot resolve (docs/mods.md, section 7e).
// An item or creature whose unit GUID is not in the game's unit index (a removed mod's, or an
// imported PC character's from mods never installed here) is dropped by the game when it loads
// the character, and its next save writes the character without it (guided run, 2026-10-08). The
// hang once blamed on such items was the game's unit index left empty (docs/mods.md, section 7e,
// cause 4). Before the guest runs, the host takes such entries out of the saves itself, with a
// backup first and the player told what went, and with safeguards against removing anything
// because of a fault of ours. No guest, OGRE or platform types.

#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

#include "mods/unit_index.h"
#include "save_import/save_tree.h"

namespace torchlight::mods {

// The units the game will know: the base index's plus the mods'. `complete` only when every source
// was read in full; otherwise nothing is removed anywhere.
struct KnownUnits {
  std::unordered_set<int64_t> guids;
  bool complete = false;
  std::string incomplete_why;
};

// From the base index (none: not read) and the mods' unit GUIDs (`mods_complete`: every mod unit
// definition read with its GUID). Also incomplete when the base index lacks any of its four groups.
KnownUnits MakeKnownUnits(const std::optional<UnitIndex>& base, const std::vector<int64_t>& mod_guids,
                          bool mods_complete, const std::string& mods_why = {});

// The units the game is expected to hold with the mods' units (`mod_units`: unit_cache.h
// ModUnitEntries; none when they were not all read, `mods_why` saying why): the base merged with
// them as the index builder merges (unit_index.h MergeUnitIndex), so a mod's unit at a base unit's
// path replaces that unit's GUID instead of adding to it. Incomplete as MakeKnownUnits is.
KnownUnits MakeExpectedUnits(const std::optional<UnitIndex>& base,
                             const std::optional<std::vector<UnitEntry>>& mod_units,
                             const std::string& mods_why = {});

// The units of the index the game loads (the merged index when mods' units made it in, the Xbox
// one otherwise): exactly what it can resolve. A mod's unit that did not get into it is unknown,
// whatever its definition says.
KnownUnits KnownUnitsOfIndex(const UnitIndex& loaded);

// The units the saves' check took as known that the game does not hold (`loaded`: the GUIDs of the
// index it actually loaded), sorted. Saved items of those units may stop a character from loading.
std::vector<int64_t> UnitsNotLoaded(const KnownUnits& assumed, const std::unordered_set<int64_t>& loaded);

struct RemovedUnit {
  std::string path;  // e.g. "player/items/item"
  std::string name;  // the save's own text for it, when it has one
  int64_t guid = 0;
};

struct UnitCheck {
  enum class Result {
    kClean,      // nothing unknown
    kRemoved,    // unknown entries taken out; `bytes` is the new file
    kLeftAlone,  // something is unknown but the file is not changed (`why`)
  };
  Result result = Result::kClean;
  std::vector<RemovedUnit> removed;
  std::vector<uint8_t> bytes;
  std::string why;
  std::string owner;  // the character's name (a character file)
};

// The checks on a parsed save (character or stash), changing its tree when entries are removed:
// - an item, a pet or a creature (an item or unit element of a list) whose unit GUID is unknown is
//   removed, and so is an unknown GUID in a list of GUIDs;
// - left alone when the known units are incomplete, when an unknown GUID is not in a list (the
//   character's own class, a quest's unit), or when most of the save's units are unknown (almost
//   surely a fault of ours, not a removed mod).
UnitCheck RemoveUnknownUnits(save_import::Parsed& parsed, const KnownUnits& known);

// The same on a recomp character file (N.TSV) or shared stash (sharedstash.bin): parsed with the
// schema (unreadable: left alone) and, when entries are removed, written back with its digest.
UnitCheck CheckCharacterFile(const save_import::Schema& schema, std::span<const uint8_t> file,
                             const KnownUnits& known);
UnitCheck CheckStashFile(const save_import::Schema& schema, std::span<const uint8_t> file,
                         const KnownUnits& known);

// What ProtectSaves did, for the log and the player's notice.
struct SaveUnitsReport {
  struct File {
    std::filesystem::path path;
    std::string owner;  // the character's name; empty for a stash
    std::vector<RemovedUnit> removed;
  };
  std::vector<File> changed;
  std::filesystem::path backup;            // where the saves were copied before
  std::vector<std::string> left_alone;     // "<file>: why"
  bool failed = false;                     // the backup or a write failed: nothing (more) written
  size_t units_not_loaded = 0;             // known to the check, missing from the index loaded
  // When saves hold units the game did not load: "<owner> (<file>)" ("" for a stash), the copy made
  // then, and whether saving is off for the session (SavesHoldingUnits, ProtectFromUnitsNotLoaded).
  std::vector<std::string> holding_not_loaded;
  std::filesystem::path not_loaded_backup;
  bool saving_blocked = false;
};

// Every save container under <user_data_root>/<profile>/<title_folder>/: character files (*.tsv)
// and shared stashes (sharedstash*.bin) checked; when any would change, the saves are backed up
// first (save-backups/<UTC>-units, save_safety.h) and each changed file is written whole and
// renamed into place. Nothing is written if the backup fails. `log` gets a line per removed entry
// and per file left alone.
SaveUnitsReport ProtectSaves(const std::filesystem::path& user_data_root, const std::string& title_folder,
                             const save_import::Schema& schema, const KnownUnits& known,
                             std::chrono::system_clock::time_point now,
                             const std::function<void(const std::string&)>& log);

// A save file holding some of the units asked for, with its owner ("" for a stash).
struct SaveHolding {
  std::filesystem::path path;
  std::string owner;
};

// The character files and shared stashes, in the same containers as ProtectSaves, that hold any
// of `guids` (read only). Unreadable files are not listed: the game cannot load them either.
std::vector<SaveHolding> SavesHoldingUnits(const std::filesystem::path& user_data_root, const std::string& title_folder,
                                           const save_import::Schema& schema, const std::unordered_set<int64_t>& guids);

// When the game loaded an index without some units the saves count on (`missing`): the game
// drops a saved item it does not know when it loads the character, and the next save writes the
// character without it. So the saves holding those units are found, all saves are copied first
// (save-backups/<UTC>-units-not-loaded), and saving is to be off for the whole session (the
// report's saving_blocked), whether or not the copy was made. Nothing in the saves is changed.
void ProtectFromUnitsNotLoaded(const std::filesystem::path& user_data_root, const std::string& title_folder,
                               const save_import::Schema& schema, const std::vector<int64_t>& missing,
                               std::chrono::system_clock::time_point now,
                               const std::function<void(const std::string&)>& log, SaveUnitsReport& report);

}  // namespace torchlight::mods
