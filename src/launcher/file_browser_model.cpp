#include "launcher/file_browser_model.h"

#include <algorithm>

namespace torchlight::launcher {

namespace {

// ASCII case-insensitive order (other bytes as they are): what a user expects of names.
bool NameLess(const std::string& a, const std::string& b) {
  return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
    const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; };
    const char lx = lower(x), ly = lower(y);
    return lx != ly ? static_cast<unsigned char>(lx) < static_cast<unsigned char>(ly)
                    : static_cast<unsigned char>(x) < static_cast<unsigned char>(y);
  });
}

bool AtTop(const std::filesystem::path& folder) { return folder == folder.root_path(); }

}  // namespace

FileBrowserModel::FileBrowserModel(Mode mode, ListFolder list, const std::filesystem::path& start,
                                   std::vector<std::filesystem::path> places)
    : mode_(mode), list_(std::move(list)), places_(std::move(places)) {
  if (!start.empty() && Open(start)) return;
  for (const auto& place : places_) {
    if (Open(place)) return;
  }
}

bool FileBrowserModel::Open(const std::filesystem::path& folder) {
  // "/a/b/" and "/a/./b" are "/a/b"; a root keeps its separator.
  std::filesystem::path normal = folder.lexically_normal();
  if (!AtTop(normal) && !normal.has_filename()) normal = normal.parent_path();
  std::vector<FolderEntry> entries;
  std::string error;
  if (!list_(normal, entries, error)) {
    error_ = {std::string(kTextCannotOpen), {{"path", normal.string()}, {"error", error}}};
    return false;
  }
  error_ = {};
  folder_ = normal;
  std::erase_if(entries, [](const FolderEntry& e) { return e.name.empty() || e.name[0] == '.'; });
  std::sort(entries.begin(), entries.end(), [](const FolderEntry& a, const FolderEntry& b) {
    if (a.folder != b.folder) return a.folder;
    return NameLess(a.name, b.name);
  });
  rows_.clear();
  if (!AtTop(folder_)) rows_.push_back({RowKind::kParent, {}, 0});
  if (mode_ == Mode::kFolder) rows_.push_back({RowKind::kUseFolder, {}, 0});
  for (const FolderEntry& e : entries) {
    rows_.push_back({e.folder ? RowKind::kFolder : RowKind::kFile, e.name, e.folder ? 0 : e.size});
  }
  selected_ = 0;
  return true;
}

void FileBrowserModel::Move(int delta) {
  if (rows_.empty()) return;
  const long long last = static_cast<long long>(rows_.size()) - 1;
  selected_ = static_cast<size_t>(std::clamp(static_cast<long long>(selected_) + delta, 0LL, last));
}

void FileBrowserModel::Select(size_t row) {
  if (row < rows_.size()) selected_ = row;
}

std::optional<std::filesystem::path> FileBrowserModel::Activate(size_t row) {
  if (row >= rows_.size()) return std::nullopt;
  const Row r = rows_[row];  // a copy: opening a folder replaces the rows
  selected_ = row;
  switch (r.kind) {
    case RowKind::kParent:
      Up();
      return std::nullopt;
    case RowKind::kUseFolder:
      return folder_;
    case RowKind::kFolder:
      Open(folder_ / r.name);
      return std::nullopt;
    case RowKind::kFile:
      // In folder mode files are shown (an extracted game is recognised by them) but not chosen.
      if (mode_ == Mode::kFile) return folder_ / r.name;
      return std::nullopt;
  }
  return std::nullopt;
}

bool FileBrowserModel::Up() {
  if (folder_.empty() || AtTop(folder_)) return false;
  return Open(folder_.parent_path());
}

void FileBrowserModel::GoTo(size_t place) {
  if (place < places_.size()) Open(places_[place]);
}

}  // namespace torchlight::launcher
