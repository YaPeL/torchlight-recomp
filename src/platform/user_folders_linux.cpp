// User folders on Linux: the XDG base directories, and a rename that never replaces.

#include <fcntl.h>
#include <stdio.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>

#include "platform/user_folders.h"

namespace torchlight::platform {

std::filesystem::path PlatformUserFolderBase(UserFolderKind kind) {
  const char* variable = nullptr;
  const char* fallback = nullptr;  // under $HOME
  switch (kind) {
    case UserFolderKind::kConfig: variable = "XDG_CONFIG_HOME", fallback = ".config"; break;
    case UserFolderKind::kState: variable = "XDG_STATE_HOME", fallback = ".local/state"; break;
    case UserFolderKind::kCache: variable = "XDG_CACHE_HOME", fallback = ".cache"; break;
    case UserFolderKind::kData:
    case UserFolderKind::kGameData: variable = "XDG_DATA_HOME", fallback = ".local/share"; break;
  }
  if (const char* xdg = std::getenv(variable); xdg && *xdg) return xdg;
  if (const char* home = std::getenv("HOME"); home && *home) {
    return std::filesystem::path(home) / fallback;
  }
  return {};
}

bool RenameNoReplace(const std::filesystem::path& from, const std::filesystem::path& to,
                     std::string& error) {
  if (renameat2(AT_FDCWD, from.c_str(), AT_FDCWD, to.c_str(), RENAME_NOREPLACE) == 0) return true;
  error = std::strerror(errno);
  return false;
}

}  // namespace torchlight::platform
