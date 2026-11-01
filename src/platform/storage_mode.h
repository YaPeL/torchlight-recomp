// Where this run keeps the player's files: the user's folders (user_folders.h) or a portable folder
// beside the application. The decision is pure (DecideStorage), over facts read once at the very
// start, before anything opens or creates a user folder.
//
// "Beside the application" means next to what the player sees: the AppImage (the executable itself
// runs from AppImage's temporary mount), the .app (never inside the bundle), or the executable.

#pragma once

#include <filesystem>
#include <string>

namespace torchlight::platform {

// The portable folder's name, beside the application; user_folders.cpp lays it out.
inline constexpr const char* kPortableFolderName = "TorchlightRecomp-data";
// In the user's configuration folder: the player chose the user's folders over a portable folder.
inline constexpr const char* kUserFoldersChoiceFile = "use-user-folders";

enum class StorageMode {
  kUserFolders,         // the user's folders, as without a portable mode
  kPortable,            // the portable folder, which exists and can be written
  kPortableUnwritable,  // it exists but cannot be written: never a silent fall back to the user's
  kAsk,                 // nothing decided yet: the player chooses
};

struct StorageFacts {
  bool test_folders = false;         // ctest's user folders (kTestUserFoldersVariable)
  bool roots_given = false;          // --user_data_root or --game_data_root: a run with its own files
  bool portable_by_default = false;  // this build's default (TORCHLIGHT_PORTABLE_DEFAULT)
  std::filesystem::path portable_folder;  // empty: no place for one (Android, unknown location)
  bool portable_exists = false;
  bool portable_writable = false;
  bool user_folders_chosen = false;  // kUserFoldersChoiceFile exists
};

StorageMode DecideStorage(const StorageFacts& facts);

// The folder the application is in. `executable_dir` as platform::ExecutableDir gives it (empty:
// unknown); `appimage_path` from --appimage_path (empty: not an AppImage), which wins.
std::filesystem::path ApplicationFolder(const std::filesystem::path& executable_dir,
                                        const std::filesystem::path& appimage_path);

// Whether a file can really be created in `folder`, an existing one (a test file, removed at once;
// permission bits alone do not tell: read-only mounts, Windows' ACLs).
bool CanWriteFolder(const std::filesystem::path& folder, std::string& error);

// This run's facts from the disk. Nothing is created; a test file is written only in a portable
// folder that exists, and only where the build's default makes it matter.
StorageFacts ReadStorageFacts(const std::filesystem::path& executable_dir,
                              const std::filesystem::path& appimage_path, bool roots_given);

}  // namespace torchlight::platform
