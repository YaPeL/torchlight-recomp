// The starting points of the launcher's file browser on Linux and macOS (BrowsePlaces in
// platform.h): plain std::filesystem, so that each platform's rule is tested on every platform over
// a made-up tree.

#pragma once

#include <filesystem>
#include <vector>

namespace torchlight::platform {

// `home`, its Downloads, every folder mounted under one of `mount_roots` (removable drives, the
// Steam Deck's SD card, macOS's volumes), by name, and `root`: only those that are folders, each
// once. A mount root is not a place of its own (/run/media/deck under /run/media), nor is a mount
// that is `root` itself (macOS's /Volumes/Macintosh HD).
std::vector<std::filesystem::path> UnixBrowsePlaces(
    const std::filesystem::path& home, const std::vector<std::filesystem::path>& mount_roots,
    const std::filesystem::path& root = "/");

}  // namespace torchlight::platform
