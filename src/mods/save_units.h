// Saved characters and stashes holding units the game cannot resolve (docs/mods.md, section 7f).
// An item or creature whose unit GUID is not in the game's unit index (a removed mod's, or an
// imported PC character's from mods never installed here) stops the game when it loads the save:
// the guest throws, and the runtime cannot unwind (patches/README.md, known gaps). Before the guest
// runs, the host takes such entries out of the saves, with safeguards against removing anything
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

}  // namespace torchlight::mods
