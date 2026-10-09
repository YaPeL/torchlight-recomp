// What the launcher (docs/launcher.md) shows and does, without drawing or doing anything itself:
// its pages, the buttons of each, and what a button, a picked path or the install's progress
// leads to. The caller (the launcher's window) carries out the returned Action: pickers, the
// install on its thread, saving the achievement set, quitting or starting the game.
//
// Pages: Install (choose the package or a folder; the last error, if any) -> Installing (progress,
// Cancel) -> Achievements (the set, asked once) -> Ready (Play). A start opens on the first page
// something is missing for; --launcher opens it on Ready when nothing is.

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "game_setup/install.h"
#include "game_setup/setup_text.h"
#include "settings/host_settings.h"

namespace torchlight::launcher {

enum class Page { kInstall, kBrowse, kInstalling, kAchievements, kReady };

enum class Button {
  kChoosePackage, kChooseFolder,     // Install: the system's picker for one or the other
  kQuit,                             // Install, Ready
  kCancel,                           // Installing: stop the install
  // Browse: to Install. Install and Achievements opened from Ready: to Ready.
  kBack,
  kAchievementsXbox, kAchievementsPc,
  kPlay, kReinstall, kChangeAchievements,  // Ready
};

// What the caller does next.
enum class Action {
  kNone,
  kPickPackage, kPickFolder,      // the system's picker; then Picked()
  kBrowse,                        // the fallback browser (Browse page, browse_source()); then Picked()
  kInstall,                       // install from install_source()/install_path(); then Progress(), Finished()
  kCancelInstall,                 // ask the running install to stop; Finished() follows
  kSaveAchievements,              // save achievements() in the host settings
  kPlay,                          // close the launcher and start the game
  kQuit,                          // close the launcher and exit
};

// What the start knows: whether the game's files are installed, the saved achievement set,
// whether --launcher asked for the launcher, and whether the game's folder is ours to install into
// (not with --game_data_root: Ready offers no reinstall then).
struct Start {
  bool game_installed = false;
  std::optional<settings::AchievementSet> achievements;
  bool on_demand = false;
  bool can_install = true;
};

// Whether the launcher opens at all: something is missing, or --launcher.
bool Needed(const Start& start);

// How a picker ended (platform::PickResult's cases).
enum class PickOutcome { kChosen, kCancelled, kFailed };

class LauncherModel {
 public:
  explicit LauncherModel(const Start& start);

  Page page() const { return page_; }
  // The buttons of the current page, in order (the first one is focused).
  std::vector<Button> Buttons() const;
  Action Press(Button button);
  // The window's close button (or the system's quit): Quit, except while installing, where it
  // cancels first and quits once the install has stopped (Finished()).
  Action Close();

  // A picker or the browser ended. kChosen: install from `path`. kCancelled: back to Install.
  // kFailed: `error` says why; the fallback browser opens (kBrowse).
  Action Picked(PickOutcome outcome, const std::string& path, const std::string& error = {});

  // The running install: its phase and bytes (`total` 0: not known yet).
  void Progress(game_setup::Phase phase, uint64_t done, uint64_t total);
  // The install ended (`result.error` empty: done; Cancelled(): cancelled; else the error for the
  // Install page).
  Action Finished(const game_setup::InstallResult& result);

  // Install page: the last error (empty: none) and the picker's failure note before the browser.
  const game_setup::Message& error() const { return error_; }
  const game_setup::Message& note() const { return note_; }
  // What to install from (kInstall) and what the browser picks (kBrowse).
  game_setup::Source install_source() const { return source_; }
  const std::string& install_path() const { return path_; }
  game_setup::Source browse_source() const { return source_; }
  // Installing page.
  game_setup::Phase phase() const { return phase_; }
  double fraction() const;  // 0 to 1
  bool cancelling() const { return cancelling_; }
  // The achievement set chosen (kSaveAchievements), or the saved one.
  const std::optional<settings::AchievementSet>& achievements() const { return achievements_; }

 private:
  Action AfterInstall();
  Action FromInstallPage(game_setup::Source source, bool picker_failed);

  Page page_ = Page::kInstall;
  std::optional<settings::AchievementSet> achievements_;
  game_setup::Message error_, note_;
  game_setup::Source source_ = game_setup::Source::kPackage;
  std::string path_;
  game_setup::Phase phase_ = game_setup::Phase::kCopying;
  uint64_t done_ = 0, total_ = 0;
  bool installed_ = false;  // the game's files are there (Install opened from Ready can go back)
  bool can_install_ = true;
  bool cancelling_ = false, quit_after_install_ = false;
  bool picker_failed_ = false;  // once failed, the buttons go straight to the browser
};

}  // namespace torchlight::launcher
