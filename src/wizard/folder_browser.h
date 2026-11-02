// The wizard's own folder browser, what it lists: for where the system's picker does not show (the
// Steam Deck's Game Mode) and for a pad. No window here (browser_page.h draws it).

#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace torchlight::wizard {

struct BrowserEntry {
  std::string name;  // UTF-8
  std::filesystem::path path;
  bool folder = false;
};

// Where Up goes from `folder`, on a machine with `root_count` filesystem roots: the folder above,
// else the list of roots where there are several (Windows' drives), else nowhere.
enum class UpMove { kAbove, kRoots, kStay };
UpMove MoveUp(const std::filesystem::path& folder, size_t root_count);

class FolderBrowser {
 public:
  // `with_files`: the files too, after the folders; otherwise only folders are listed.
  explicit FolderBrowser(bool with_files) : with_files_(with_files) {}

  void Open(const std::filesystem::path& folder);
  // As MoveUp says.
  void Up();

  const std::filesystem::path& folder() const { return folder_; }
  // Showing the roots, not a folder.
  bool at_roots() const { return at_roots_; }
  const std::vector<BrowserEntry>& entries() const { return entries_; }
  // Why the folder could not be read (whole or in part); empty when it could.
  const std::string& error() const { return error_; }

 private:
  bool with_files_;
  bool at_roots_ = false;
  std::filesystem::path folder_;
  std::vector<BrowserEntry> entries_;
  std::string error_;
};

}  // namespace torchlight::wizard
