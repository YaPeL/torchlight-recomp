// Draws the launcher's model with ImGui (docs/launcher.md, stage 1, step 3): one page at a time in
// a window that fills the display, a centered column, the project's name on top and our own
// neutral colors (nothing of the game's). Only ImGui: the window, SDL and what the buttons do are
// the caller's (launcher.h).
//
// Focus: the first button of a page when it opens (the model's order); the browser's selected row
// when a folder opens. A or Enter activates (ImGui's navigation); B or Escape goes back: up a
// folder in the browser (from the top, back to Install), else the page's Back button, if any.

#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "game_setup/setup_text.h"
#include "launcher/file_browser_model.h"
#include "launcher/launcher_model.h"

namespace torchlight::launcher {

// What the user did in a frame.
struct ViewResult {
  std::optional<Button> button;                 // for LauncherModel::Press
  std::optional<std::filesystem::path> chosen;  // the browser's choice, for Picked
};

class LauncherView {
 public:
  // One frame, between ImGui::NewFrame and ImGui::Render. `browser` is the open browser (Browse
  // page; its folder and selection change here); `game_dir` is shown on Install and Ready; `scale`
  // is the display's (LauncherWindow::scale).
  ViewResult Draw(const LauncherModel& model, FileBrowserModel* browser,
                  const game_setup::Translate& tr, const std::string& game_dir, float scale);

 private:
  std::optional<Page> page_;      // the page drawn last frame
  std::filesystem::path folder_;  // the browser's folder drawn last frame
};

// "512 B", "1.5 KB", "4.2 MB", "1.6 GB" (powers of 1024).
std::string FormatSize(uint64_t bytes);

}  // namespace torchlight::launcher
