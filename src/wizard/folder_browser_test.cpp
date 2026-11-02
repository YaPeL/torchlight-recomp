// The browser's listing, on invented folders below the system's temporary folder.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "wizard/folder_browser.h"

namespace fs = std::filesystem;
using torchlight::wizard::FolderBrowser;

namespace {

void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    std::exit(1);
  }
}

}  // namespace

int main() {
  const fs::path root = fs::temp_directory_path() / "tl_folder_browser_test";
  fs::remove_all(root);
  fs::create_directories(root / "b folder");
  fs::create_directories(root / "a folder" / "inner");
  fs::create_directories(root / fs::path(u8"ñandú"));
  std::ofstream(root / "a file.txt") << "x";
  std::error_code link_ec;
  fs::create_symlink(root / "nowhere", root / "broken link", link_ec);  // not allowed everywhere

  FolderBrowser folders(false);
  folders.Open(root);
  Check(folders.error().empty(), "a folder reads");
  Check(folders.entries().size() == 3, "only folders; a broken link does not end the listing");
  Check(folders.entries()[0].name == "a folder" && folders.entries()[1].name == "b folder",
        "sorted by name");
  Check(folders.entries()[2].name == reinterpret_cast<const char*>(u8"ñandú"), "names are UTF-8");

  FolderBrowser with_files(true);
  with_files.Open(root);
  Check(with_files.entries().size() == (link_ec ? 4u : 5u), "the files too");
  Check(with_files.entries()[2].folder && !with_files.entries()[3].folder, "folders first");

  folders.Open(root / "a folder" / "inner");
  folders.Up();
  Check(folders.folder() == root / "a folder", "up: the folder above");

  folders.Open(root / "not there");
  Check(!folders.error().empty() && folders.entries().empty(), "a missing folder says so");

  // At a root: the roots where there are several (Windows' drives), else it stays.
  fs::path top = root;
  while (top.parent_path() != top) top = top.parent_path();
  folders.Open(top);
  folders.Up();
  Check(folders.at_roots() ? !folders.entries().empty() : folders.folder() == top,
        "above a root");
  folders.Up();
  fs::remove_all(root);
  std::printf("folder_browser_test: ok\n");
  return 0;
}
