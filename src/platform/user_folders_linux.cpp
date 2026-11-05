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

std::filesystem::path HomeFolder() {
  const char* home = std::getenv("HOME");
  return home ? std::filesystem::path(home) : std::filesystem::path();
}

std::vector<std::filesystem::path> FolderRoots() {
  // Where a removable disk appears (the Steam Deck's SD card: /run/media/deck/<label>), which a pad
  // should not have to walk to from "/".
  std::vector<std::filesystem::path> roots = {"/"};
  std::error_code ec;
  for (const char* place : {"/run/media", "/media", "/mnt"}) {
    if (std::filesystem::is_directory(place, ec)) roots.emplace_back(place);
  }
  return roots;
}

bool RenameNoReplace(const std::filesystem::path& from, const std::filesystem::path& to,
                     std::string& error) {
  if (renameat2(AT_FDCWD, from.c_str(), AT_FDCWD, to.c_str(), RENAME_NOREPLACE) == 0) return true;
  error = std::strerror(errno);
  return false;
}

}  // namespace torchlight::platform
