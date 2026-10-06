// What the in-game import does with the files in <game_data_root>/import/ (docs/saves-research.md,
// section 4d): finding them, converting them, deciding what goes where, and recording the user's
// decision. No guest, VFS or UI types: the menu gives a view of the save container and writes the
// characters through the game's mount; the stash and settings are applied at the next start
// (ApplyPending), because the game keeps them in memory and writes them back.

#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "save_import/game_data.h"
#include "save_import/save_tree.h"
#include "save_import/settings_file.h"

namespace torchlight::save_import {

// Suffixes the import gives to files in the import folder (never in a PC installation).
inline constexpr const char* kImportedSuffix = ".bkp";       // imported
inline constexpr const char* kRejectedSuffix = ".rejected";  // cannot be converted (see import.log)
inline constexpr const char* kSkippedSuffix = ".skipped";    // "do not ask again"
inline constexpr const char* kPendingFolder = "pending";     // decisions applied at the next start
inline constexpr const char* kLogFile = "import.log";

// Why something confirmed in the menu was not applied at the next start (the menu explains it and
// asks again).
struct Notice {
  enum class Kind {
    kStashChanged,      // the recomp stash `file` has `now` items, `then` when it was confirmed
    kContainerMissing,  // the save container recorded at confirmation no longer exists
    kUnreadable,        // `file` (recomp or PC) could not be read
  };
  Kind kind = Kind::kUnreadable;
  std::string file;
  int now = 0, then = 0;
};

// What the import folder holds that has not been imported, rejected or skipped yet.
struct ImportFolder {
  std::filesystem::path folder;
  std::vector<std::filesystem::path> characters;  // N.SVT
  std::vector<std::filesystem::path> stashes;     // sharedstash.bin, sharedstashh.bin
  std::vector<std::filesystem::path> settings;    // settings.txt, local_settings.txt
  std::optional<std::filesystem::path> pc_pak;    // Pak.zip
  bool settings_pending = false;                  // already confirmed, waiting for the next start
  std::vector<Notice> notices;                    // why something was not applied at the start

  bool HasWork() const { return !characters.empty() || !stashes.empty() || !settings.empty(); }
};

// Looks at the folder (names compared without case). Files already pending are left out.
ImportFolder ScanImportFolder(const std::filesystem::path& folder);

// The save container as the menu sees it (through the game's own mount).
struct ContainerView {
  std::vector<std::string> file_names;          // every file in it
  std::map<std::string, Bytes> stashes;         // lower-case name -> contents, if present
  std::map<std::string, Bytes> settings;        // settings file name -> contents, if present
};

struct CharacterImport {
  std::filesystem::path source;
  std::string name, class_name;      // from the save, for the message
  int number = -1;                   // the N of the N.TSV it becomes
  Bytes converted;                   // the 360 file
  std::vector<std::string> changes;  // quest dialog adaptations
  std::vector<std::string> problems;  // non-empty: rejected
  bool ok() const { return problems.empty(); }
  std::string destination() const { return std::to_string(number) + ".TSV"; }
};

struct StashImport {
  std::filesystem::path source;
  std::string destination;  // sharedstash.bin or sharedstashh.bin
  int pc_items = 0;
  int recomp_items = 0;     // in the recomp stash now (0 if there is none)
  Bytes converted;
  std::vector<std::string> problems;
  bool ok() const { return problems.empty(); }
  bool needs_replace_confirmation() const { return ok() && recomp_items > 0; }
};

struct SettingsImport {
  std::vector<std::filesystem::path> sources;
  std::map<std::string, SettingsPlan> plans;  // per recomp settings file
  std::vector<std::string> problems;
  bool ok() const { return problems.empty(); }
  bool HasChanges() const;
};

struct ImportPlan {
  // Non-empty: nothing can be imported (no PC Pak.zip, an unreadable pak); for the message.
  std::string blocked;
  bool missing_pc_pak = false;
  std::vector<CharacterImport> characters;
  std::vector<StashImport> stashes;
  std::optional<SettingsImport> settings;
  std::vector<Notice> notices;  // from the last start, to explain why it asks again

  bool HasSomethingToAsk() const;
};

// Converts and decides. `game_pak` is the 360 pak.zip of game_data_root.
ImportPlan BuildImportPlan(const Schema& schema, const ImportFolder& folder,
                           const std::filesystem::path& game_pak, const ContainerView& container);

// The number the game gives a new character (sub_8238EA88): the lowest N >= 0 not taken by an
// "N.tsv" in the container (N read like wcstol: leading digits, 0 if none) nor by `also_taken`.
int FreeCharacterNumber(const std::vector<std::string>& file_names, const std::vector<int>& also_taken);

using Log = std::function<void(const std::string&)>;

// "Yes": records the stash and settings for the next start (converted stash in pending/, the
// container's host path, the recomp item count it was confirmed against) and renames the files
// handled now: imported characters (written by the caller before) -> .bkp, rejected -> .rejected.
// `replace_stash` says, per stash destination, whether the user accepted replacing a recomp stash
// that has items (a stash that needs it and was not accepted stays in the folder).
bool ConfirmImport(const ImportPlan& plan, const ImportFolder& folder,
                   const std::filesystem::path& container_path,
                   const std::map<std::string, bool>& replace_stash, const Log& log,
                   std::string& error);

// "Do not ask again": every file of the plan becomes .skipped.
bool SkipImport(const ImportPlan& plan, const ImportFolder& folder, const Log& log, std::string& error);

// At the next start, before the guest runs: applies the confirmed stash and settings if the
// container still looks as when they were confirmed; otherwise leaves a notice for the menu, which
// asks again. Returns the notices left.
std::vector<Notice> ApplyPending(const std::filesystem::path& folder, const Schema& schema,
                                      const Log& log);

// Appends a line with the UTC time to import.log in `folder`.
void AppendImportLog(const std::filesystem::path& folder, const std::string& line);

}  // namespace torchlight::save_import
