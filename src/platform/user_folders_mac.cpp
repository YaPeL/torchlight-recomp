// User folders on macOS: Apple's places in the user's Library, and a rename that never replaces.

#include <pwd.h>
#include <stdio.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>

#include "platform/user_folders.h"

namespace torchlight::platform {

namespace {

std::filesystem::path Home() {
  if (const char* home = std::getenv("HOME"); home && *home) return home;
  if (const passwd* entry = getpwuid(getuid()); entry && entry->pw_dir) return entry->pw_dir;
  return {};
}

}  // namespace

// Settings, the runtime's user data and the game's files in Application Support (the user data and
// the settings share TorchlightRecomp/, their names apart); the shaders in Caches; the logs in Logs.
std::filesystem::path PlatformUserFolderBase(UserFolderKind kind) {
  const std::filesystem::path home = Home();
  if (home.empty()) return {};
  const std::filesystem::path library = home / "Library";
  switch (kind) {
    case UserFolderKind::kConfig:
    case UserFolderKind::kData:
    case UserFolderKind::kGameData: return library / "Application Support";
    case UserFolderKind::kState: return library / "Logs";
    case UserFolderKind::kCache: return library / "Caches";
  }
  return {};
}

bool RenameNoReplace(const std::filesystem::path& from, const std::filesystem::path& to,
                     std::string& error) {
  if (renamex_np(from.c_str(), to.c_str(), RENAME_EXCL) == 0) return true;
  error = std::strerror(errno);
  return false;
}

}  // namespace torchlight::platform
