// Tests for reading folders from the disk for the launcher's file browser (folder_listing.h), in a
// temporary folder: folders and files with sizes, a name with letters outside ASCII (UTF-8 both
// ways, also through the browser model), and folders that cannot be read.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "launcher/file_browser_model.h"
#include "launcher/folder_listing.h"

namespace {

namespace fs = std::filesystem;
using namespace torchlight::launcher;

int failures = 0;
void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

const FolderEntry* Find(const std::vector<FolderEntry>& entries, const std::string& name) {
  for (const FolderEntry& e : entries) {
    if (e.name == name) return &e;
  }
  return nullptr;
}

// "ñandú" in UTF-8, as the model's names are.
const std::string kName = "\xC3\xB1" "and\xC3\xBA";

void TestListing(const fs::path& base) {
  const fs::path accented = base / fs::path(u8"ñandú");
  fs::create_directories(accented / "inner");
  std::ofstream(base / "b.bin", std::ios::binary) << "12345";
  std::ofstream(accented / "LIVEPKG", std::ios::binary) << "x";
  std::vector<FolderEntry> entries;
  std::string error;
  Check(ListFolderOnDisk(base, entries, error) && error.empty(), "the folder reads");
  Check(entries.size() == 2, "its two entries");
  const FolderEntry* folder = Find(entries, kName);
  Check(folder && folder->folder && folder->size == 0, "a folder, its name in UTF-8");
  const FolderEntry* file = Find(entries, "b.bin");
  Check(file && !file->folder && file->size == 5, "a file with its size");

  // Through the model: the UTF-8 name back to a path that opens.
  FileBrowserModel browser(FileBrowserModel::Mode::kFile, ListFolderOnDisk, base, {});
  size_t row = browser.rows().size();
  for (size_t i = 0; i < browser.rows().size(); ++i) {
    if (browser.rows()[i].name == kName) row = i;
  }
  Check(!browser.Activate(row) && browser.error().empty(), "the model opens the accented folder");
  Check(fs::equivalent(browser.folder(), accented), "and is in it");
  Check(browser.rows().size() == 3 && browser.rows()[2].name == "LIVEPKG", "with its rows");
  const auto chosen = browser.Activate(2);
  Check(chosen && fs::equivalent(*chosen, accented / "LIVEPKG"), "a file inside is chosen");
}

void TestUnreadable(const fs::path& base) {
  std::vector<FolderEntry> entries{{"stale", false, 0}};
  std::string error;
  Check(!ListFolderOnDisk(base / "missing", entries, error) && !error.empty() && entries.empty(),
        "a missing folder: false, with the reason, no entries");
  error.clear();
  Check(!ListFolderOnDisk(base / "b.bin", entries, error) && !error.empty(),
        "a file is not a folder");

  // A folder without permissions: whatever the system says (root and Windows can still read it),
  // the listing agrees with it.
  const fs::path locked = base / "locked";
  fs::create_directory(locked);
  std::error_code ec;
  fs::permissions(locked, fs::perms::none, ec);
  fs::directory_iterator probe(locked, ec);
  error.clear();
  const bool read = ListFolderOnDisk(locked, entries, error);
  Check(read == !ec && read == error.empty(), "a folder without permissions: as the system says");
  fs::permissions(locked, fs::perms::owner_all, ec);
}

}  // namespace

int main() {
  const fs::path base = fs::temp_directory_path() / "folder_listing_test";
  std::error_code ec;
  fs::remove_all(base, ec);
  fs::create_directories(base);
  TestListing(base);
  TestUnreadable(base);
  fs::remove_all(base, ec);
  if (failures) return 1;
  std::printf("folder listing test: ok\n");
  return 0;
}
