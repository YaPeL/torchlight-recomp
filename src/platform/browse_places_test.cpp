// Tests for the file browser's starting points on Linux and macOS (browse_places.h), over a
// made-up tree in a temporary folder: home and Downloads, the mounted drives in each platform's
// places (the Deck's SD card, macOS's volumes), the mount roots and the startup disk left out, the
// root last, missing folders skipped.

#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "platform/browse_places.h"

namespace {

namespace fs = std::filesystem;
using torchlight::platform::UnixBrowsePlaces;

int failures = 0;
void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

std::string Names(const std::vector<fs::path>& places, const fs::path& root) {
  std::string out;
  for (const fs::path& place : places) {
    if (!out.empty()) out += ",";
    out += place == root ? "<root>" : place.lexically_relative(root).generic_string();
  }
  return out;
}

void TestLinux(const fs::path& root) {
  fs::create_directories(root / "home/deck/Downloads");
  fs::create_directories(root / "run/media/deck/SD");
  fs::create_directories(root / "run/media/deck/USB");
  const fs::path home = root / "home/deck";
  const std::vector<fs::path> mount_roots = {root / "run/media/deck", root / "run/media",
                                             root / "media/deck", root / "media"};
  Check(Names(UnixBrowsePlaces(home, mount_roots, root), root) ==
            "home/deck,home/deck/Downloads,run/media/deck/SD,run/media/deck/USB,<root>",
        "Linux: home, Downloads, the drives by name, not the user's mount root, the root");
  fs::create_directories(root / "media/Stick");
  Check(Names(UnixBrowsePlaces(home, mount_roots, root), root) ==
            "home/deck,home/deck/Downloads,run/media/deck/SD,run/media/deck/USB,media/Stick,<root>",
        "Linux: a drive mounted in /media itself");
}

void TestMac(const fs::path& root) {
  fs::create_directories(root / "Users/martin");
  fs::create_directories(root / "Volumes/Data");
  std::error_code ec;
  fs::create_directory_symlink(root, root / "Volumes/Macintosh HD", ec);
  const bool linked = !ec;  // Windows without the right to make links: the rest still checked
  Check(Names(UnixBrowsePlaces(root / "Users/martin", {root / "Volumes"}, root), root) ==
            "Users/martin,Volumes/Data,<root>",
        linked ? "macOS: no Downloads, the volumes but the startup disk, the root"
               : "macOS: no Downloads, the volumes, the root");
}

void TestNothing(const fs::path& root) {
  Check(Names(UnixBrowsePlaces(root / "nobody", {root / "nowhere"}, root), root) == "<root>",
        "no home, no drives: the root only");
  Check(Names(UnixBrowsePlaces({}, {}, root), root) == "<root>", "an empty home");
}

}  // namespace

int main() {
  const fs::path base = fs::temp_directory_path() / "browse_places_test";
  std::error_code ec;
  fs::remove_all(base, ec);
  TestLinux(base / "linux");
  TestMac(base / "mac");
  fs::create_directories(base / "empty");
  TestNothing(base / "empty");
  fs::remove_all(base, ec);
  if (failures) return 1;
  std::printf("browse places test: ok\n");
  return 0;
}
