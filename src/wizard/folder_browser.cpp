#include "wizard/folder_browser.h"

#include <algorithm>
#include <system_error>

#include "platform/user_folders.h"

namespace fs = std::filesystem;

namespace torchlight::wizard {
namespace {

std::string Utf8(const fs::path& path) {
  const std::u8string text = path.u8string();
  return std::string(text.begin(), text.end());
}

}  // namespace

UpMove MoveUp(const fs::path& folder, size_t root_count) {
  const fs::path above = folder.parent_path();
  if (!above.empty() && above != folder) return UpMove::kAbove;
  return root_count > 1 ? UpMove::kRoots : UpMove::kStay;
}

void FolderBrowser::Open(const fs::path& given) {
  // Without a last separator: its parent_path would be the same folder.
  fs::path folder = given;
  if (!folder.has_filename() && folder.has_relative_path()) folder = folder.parent_path();
  at_roots_ = false;
  folder_ = folder;
  entries_.clear();
  error_.clear();
  std::vector<BrowserEntry> files;
  std::error_code ec;
  for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
    // Its own code: a broken link is a file here, and does not end the listing.
    std::error_code entry_ec;
    const bool is_folder = it->is_directory(entry_ec);
    if (is_folder) {
      entries_.push_back({Utf8(it->path().filename()), it->path(), true});
    } else if (with_files_) {
      files.push_back({Utf8(it->path().filename()), it->path(), false});
    }
  }
  if (ec) error_ = "Cannot read " + Utf8(folder) + ": " + ec.message();
  const auto by_name = [](const BrowserEntry& a, const BrowserEntry& b) { return a.name < b.name; };
  std::sort(entries_.begin(), entries_.end(), by_name);
  std::sort(files.begin(), files.end(), by_name);
  entries_.insert(entries_.end(), files.begin(), files.end());
}

void FolderBrowser::Up() {
  if (at_roots_) return;
  const std::vector<fs::path> roots = platform::FolderRoots();
  const UpMove move = MoveUp(folder_, roots.size());
  if (move == UpMove::kAbove) Open(folder_.parent_path());
  if (move != UpMove::kRoots) return;
  at_roots_ = true;
  entries_.clear();
  error_.clear();
  for (const fs::path& root : roots) entries_.push_back({Utf8(root), root, true});
}

}  // namespace torchlight::wizard
