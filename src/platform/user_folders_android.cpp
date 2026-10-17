// User folders on Android. Where they go (the app's internal or external storage, which SDL gets
// from the activity) is stage A1 of docs/android-port.md: until then there are none, and every
// folder of platform.h is empty. Renames never replace, as on Linux (renameat2, API 30).

#include <fcntl.h>
#include <stdio.h>

#include <cerrno>
#include <cstring>

#include "platform/user_folders.h"

namespace torchlight::platform {

std::filesystem::path PlatformUserFolderBase(UserFolderKind) { return {}; }

bool RenameNoReplace(const std::filesystem::path& from, const std::filesystem::path& to,
                     std::string& error) {
  if (renameat2(AT_FDCWD, from.c_str(), AT_FDCWD, to.c_str(), RENAME_NOREPLACE) == 0) return true;
  error = std::strerror(errno);
  return false;
}

}  // namespace torchlight::platform
