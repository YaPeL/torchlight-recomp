// The user's folders (configuration, state and logs, cache, data), by kind, and their migration
// from the name they had before.
//
// They were named "torchlight", the name the PC game's folders may also have; they are now
// "TorchlightRecomp". At startup, before anything opens or creates them, each old folder is
// renamed to the new name: only when the new one does not exist (with both, nothing is touched and
// the log says so), only when the old one holds something this project writes there (a folder of
// the same name could be another program's), never replacing or deleting anything.

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace torchlight::platform {

inline constexpr const char* kUserFolderName = "TorchlightRecomp";
inline constexpr const char* kLegacyUserFolderName = "torchlight";

enum class UserFolderKind {
  kConfig,  // settings.toml, the runtime's torchlight.toml
  kState,   // logs/
  kCache,   // ogre/ (shaders), layouts/
  kData,    // the runtime's user data: saves, save-backups, profiles, its cache/
  // The game's files the first start installs from the user's package (~200 MB; its import/ is
  // where saves to import go): a folder of its own, since on Windows the user data lives where
  // saves belong and the game's files do not.
  kGameData,
};

// Tests only: when this environment variable is set (ctest sets it for every test, from the top
// CMakeLists.txt), every user folder lives below it, in config/, state/, cache/, data/ and
// game_data/, so no test writes to the user's real folders. Checked first, on every platform,
// before the platform's own folders (which on Windows are known folders that ignore the
// environment). Nothing else sets it; it is the project's only environment variable, and only for
// that.
inline constexpr const char* kTestUserFoldersVariable = "TORCHLIGHT_TEST_USER_FOLDERS";

// The base directory a kind of user folder lives in, not created; empty when there is none:
// below kTestUserFoldersVariable when set, else the platform's (PlatformUserFolderBase).
std::filesystem::path UserFolderBase(UserFolderKind kind);
// The platform's own base directory (Linux: the XDG base directory; one implementation file per
// platform).
std::filesystem::path PlatformUserFolderBase(UserFolderKind kind);
// <base>/TorchlightRecomp (kGameData: <base>/TorchlightRecomp/game), not created; empty when there
// is no base. Linux: kGameData below the data base, ~/.local/share/TorchlightRecomp/game/.
std::filesystem::path UserFolder(UserFolderKind kind);

struct LegacyFolder {
  std::filesystem::path old_path, new_path;
  // Entries one of which the old folder must have to be ours: names, or "<16 hex digits>" for a
  // profile folder (an XUID).
  std::vector<std::string> markers;
};

enum class Migration { kNothing, kMoved, kBothExist, kNotOurs, kFailed };
struct MigrationResult {
  Migration outcome = Migration::kNothing;
  std::string message;  // for the log; empty with kNothing
};

// Renames old_path to new_path when the old folder exists and is ours and the new one does not; the
// rename never replaces anything (it fails instead, as it does across file systems).
MigrationResult MigrateFolder(const LegacyFolder& folder);
// The old folders of every kind with their new names and markers.
std::vector<LegacyFolder> LegacyUserFolders();
// A message for the log from before logging is up, with its level: a warning only for a real
// problem.
struct StartupMessage {
  bool warning = false;
  std::string text;
};
// MigrateFolder on each of them; the messages to log (logging is not up yet at that point). Warnings:
// a rename that failed, and both folders existing for anything but the cache (the old one may hold
// data no longer used).
std::vector<StartupMessage> MigrateUserFolders();

}  // namespace torchlight::platform
