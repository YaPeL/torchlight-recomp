// The launcher's own file browser, for when the system's picker cannot be shown (the Steam Deck's
// Game Mode has none): one folder's entries at a time, navigated with a pad, a keyboard or a
// mouse. Picks a file (the package) or a folder (an extracted game). Reading folders is the
// caller's (`ListFolder`, the platform module's in the launcher, a fake in the tests); the
// starting points (drives on Windows; the home folder and / elsewhere) too.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "game_setup/setup_text.h"

namespace torchlight::launcher {

struct FolderEntry {
  std::string name;  // UTF-8
  bool folder = false;
  uint64_t size = 0;  // files only
};
// Fills `entries` with the folder's entries (any order); false with `error` when it cannot be read.
using ListFolder = std::function<bool(const std::filesystem::path& folder,
                                      std::vector<FolderEntry>& entries, std::string& error)>;

class FileBrowserModel {
 public:
  enum class Mode { kFile, kFolder };
  // A row of the list: the parent folder, "use this folder" (folder mode), a subfolder or a file.
  enum class RowKind { kParent, kUseFolder, kFolder, kFile };
  struct Row {
    RowKind kind;
    std::string name;  // empty for kParent and kUseFolder
    uint64_t size = 0;
  };

  // Opens `start`; when it cannot be read, the first of `places` that can.
  FileBrowserModel(Mode mode, ListFolder list, const std::filesystem::path& start,
                   std::vector<std::filesystem::path> places);

  const std::filesystem::path& folder() const { return folder_; }
  const std::vector<Row>& rows() const { return rows_; }
  const std::vector<std::filesystem::path>& places() const { return places_; }
  // The last folder that could not be opened (empty: none).
  const game_setup::Message& error() const { return error_; }

  // The selected row (the pad's focus), kept in range; 0 after every change of folder.
  size_t selected() const { return selected_; }
  void Move(int delta);  // stops at the ends
  void Select(size_t row);

  // Activates a row: a folder or the parent opens; a file (file mode) or "use this folder" (folder
  // mode) is the choice, returned.
  std::optional<std::filesystem::path> Activate(size_t row);
  std::optional<std::filesystem::path> ActivateSelected() { return Activate(selected_); }
  // Back to the parent folder (the pad's B); false at the top.
  bool Up();
  // Opens one of places().
  void GoTo(size_t place);

  // The error's English text, for the translation check.
  static constexpr std::string_view kTextCannotOpen = "Cannot open {path}: {error}";

 private:
  bool Open(const std::filesystem::path& folder);

  Mode mode_;
  ListFolder list_;
  std::vector<std::filesystem::path> places_;
  std::filesystem::path folder_;
  std::vector<Row> rows_;
  size_t selected_ = 0;
  game_setup::Message error_;
};

}  // namespace torchlight::launcher
