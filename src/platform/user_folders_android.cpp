// User folders on Android: the app's own storage, which SDL asks the activity for (JNI), so this
// file links SDL3 there. docs/android-port.md, stage A1.2:
// - settings, saves and the installed game in internal storage (Context.getFilesDir; private, and
//   deleted with the app);
// - logs in the external app-specific storage (Context.getExternalFilesDir), which `adb pull` reads
//   without root; internal storage when there is none mounted;
// - the cache in the app's cache folder (Context.getCacheDir), which the system may clear.
// Renames never replace, as on Linux (renameat2, API 30).

#include <fcntl.h>
#include <stdio.h>

#include <cerrno>
#include <cstring>

#include <SDL3/SDL_system.h>

#include "platform/user_folders.h"

namespace torchlight::platform {

namespace {
std::filesystem::path Path(const char* path) { return path && *path ? path : ""; }
}  // namespace

std::filesystem::path PlatformUserFolderBase(UserFolderKind kind) {
  switch (kind) {
    case UserFolderKind::kConfig:
    case UserFolderKind::kData:
    case UserFolderKind::kGameData: return Path(SDL_GetAndroidInternalStoragePath());
    case UserFolderKind::kState:
      if (SDL_GetAndroidExternalStorageState() & SDL_ANDROID_EXTERNAL_STORAGE_WRITE) {
        if (auto external = Path(SDL_GetAndroidExternalStoragePath()); !external.empty()) {
          return external;
        }
      }
      return Path(SDL_GetAndroidInternalStoragePath());
    case UserFolderKind::kCache: return Path(SDL_GetAndroidCachePath());
  }
  return {};
}

// No folder browser on Android (its first start uses the system's picker).
std::filesystem::path HomeFolder() { return {}; }
std::vector<std::filesystem::path> FolderRoots() { return {}; }

bool RenameNoReplace(const std::filesystem::path& from, const std::filesystem::path& to,
                     std::string& error) {
  if (renameat2(AT_FDCWD, from.c_str(), AT_FDCWD, to.c_str(), RENAME_NOREPLACE) == 0) return true;
  error = std::strerror(errno);
  return false;
}

}  // namespace torchlight::platform
