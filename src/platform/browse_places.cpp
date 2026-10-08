#include "platform/browse_places.h"

#include <algorithm>
#include <system_error>

namespace torchlight::platform {

std::vector<std::filesystem::path> UnixBrowsePlaces(
    const std::filesystem::path& home, const std::vector<std::filesystem::path>& mount_roots,
    const std::filesystem::path& root) {
  namespace fs = std::filesystem;
  std::vector<fs::path> places;
  const auto add = [&](const fs::path& place) {
    std::error_code ec;
    if (place.empty() || !fs::is_directory(place, ec)) return;
    for (const fs::path& known : places) {
      if (fs::equivalent(known, place, ec)) return;
    }
    places.push_back(place);
  };
  add(home);
  if (!home.empty()) add(home / "Downloads");
  for (const fs::path& mount_root : mount_roots) {
    std::error_code ec;
    std::vector<fs::path> mounts;
    for (fs::directory_iterator it(mount_root, ec), end; !ec && it != end; it.increment(ec)) {
      mounts.push_back(it->path());
    }
    std::sort(mounts.begin(), mounts.end());
    for (const fs::path& mount : mounts) {
      const bool is_root = fs::equivalent(mount, root, ec);
      const bool is_mount_root = std::any_of(
          mount_roots.begin(), mount_roots.end(),
          [&](const fs::path& other) { return fs::equivalent(mount, other, ec); });
      if (!is_root && !is_mount_root) add(mount);
    }
  }
  add(root);
  return places;
}

}  // namespace torchlight::platform
