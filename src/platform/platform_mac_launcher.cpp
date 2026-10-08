// macOS: the launcher's part of the platform module (platform.h, "launcher"). Not built yet: the
// macOS platform module (docs/macos-port.md) adds it to torchlight_platform, or moves these into
// its own file. Its rule for the browser's places is tested on every platform
// (browse_places_test, the macOS case).

#include "platform/browse_places.h"
#include "platform/platform.h"

#include <cstdlib>
#include <filesystem>
#include <vector>

#include <pwd.h>
#include <unistd.h>

namespace torchlight::platform {

bool PreferFullscreenLauncher() { return false; }

std::vector<std::filesystem::path> BrowsePlaces() {
  std::filesystem::path home;
  if (const passwd* entry = getpwuid(getuid()); entry && entry->pw_dir) home = entry->pw_dir;
  if (const char* variable = std::getenv("HOME"); variable && *variable) home = variable;
  // Every mounted volume is in /Volumes, the startup disk too (a link to /, left out).
  return UnixBrowsePlaces(home, {"/Volumes"});
}

}  // namespace torchlight::platform
