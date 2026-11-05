// The wizard's own folder browser as a page: played with a pad, a mouse or a keyboard.

#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "wizard/folder_browser.h"
#include "wizard/wizard_ui.h"

namespace torchlight::wizard {

struct BrowserTexts {
  std::string above = "..  (the folder above)";
  std::string this_computer = "This computer";
  std::string use_folder = "Use this folder";
  std::string folder_mark = "[folder]  ";
  std::string file_mark = "            ";
};

class BrowserPage {
 public:
  explicit BrowserPage(bool with_files) : browser_(with_files) {}
  BrowserTexts texts;

  // Before going to the page.
  void Open(const std::filesystem::path& folder) {
    browser_.Open(folder);
    refocus_frames_ = kRefocusFrames;
  }
  const std::filesystem::path& folder() const { return browser_.folder(); }
  // Draws the page; the folder or file once one is chosen. The caller then leaves the page.
  std::optional<std::filesystem::path> Draw(Ui& ui, const std::string& title,
                                            const std::string& step);

 private:
  // The list's items change with the folder, and the pad's focus would be lost with them: its first
  // item takes it on the next frames (ImGui takes it from the second, Ui::Frame).
  static constexpr int kRefocusFrames = 2;
  FolderBrowser browser_;
  int refocus_frames_ = kRefocusFrames;
};

}  // namespace torchlight::wizard
