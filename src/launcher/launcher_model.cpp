#include "launcher/launcher_model.h"

#include <algorithm>

#include "game_setup/game_files.h"

namespace torchlight::launcher {

using game_setup::Source;

bool Needed(const Start& start) {
  return start.on_demand || !start.game_installed || !start.achievements;
}

LauncherModel::LauncherModel(const Start& start)
    : achievements_(start.achievements),
      installed_(start.game_installed),
      can_install_(start.can_install) {
  if (!installed_) {
    page_ = Page::kInstall;
  } else if (!achievements_) {
    page_ = Page::kAchievements;
  } else {
    page_ = Page::kReady;
  }
}

std::vector<Button> LauncherModel::Buttons() const {
  switch (page_) {
    case Page::kInstall:
      if (installed_) return {Button::kChoosePackage, Button::kChooseFolder, Button::kBack};
      return {Button::kChoosePackage, Button::kChooseFolder, Button::kQuit};
    case Page::kBrowse:
      return {Button::kBack};
    case Page::kInstalling:
      return cancelling_ ? std::vector<Button>{} : std::vector<Button>{Button::kCancel};
    case Page::kAchievements:
      // Opened from Ready (a set is saved): it can go back unchanged.
      if (installed_ && achievements_) {
        return {Button::kAchievementsXbox, Button::kAchievementsPc, Button::kBack};
      }
      return {Button::kAchievementsXbox, Button::kAchievementsPc};
    case Page::kReady:
      if (!can_install_) return {Button::kPlay, Button::kChangeAchievements, Button::kQuit};
      return {Button::kPlay, Button::kChangeAchievements, Button::kReinstall, Button::kQuit};
  }
  return {};
}

Action LauncherModel::FromInstallPage(Source source, bool picker_failed) {
  source_ = source;
  error_ = {};
  if (picker_failed) {
    page_ = Page::kBrowse;
    return Action::kBrowse;
  }
  return source == Source::kPackage ? Action::kPickPackage : Action::kPickFolder;
}

Action LauncherModel::Press(Button button) {
  const std::vector<Button> buttons = Buttons();
  if (std::find(buttons.begin(), buttons.end(), button) == buttons.end()) return Action::kNone;
  switch (button) {
    case Button::kChoosePackage:
      return FromInstallPage(Source::kPackage, picker_failed_);
    case Button::kChooseFolder:
      return FromInstallPage(Source::kFolder, picker_failed_);
    case Button::kQuit:
      return Action::kQuit;
    case Button::kCancel:
      cancelling_ = true;
      return Action::kCancelInstall;
    case Button::kBack:
      page_ = page_ == Page::kBrowse ? Page::kInstall : Page::kReady;
      error_ = {};
      return Action::kNone;
    case Button::kAchievementsXbox:
    case Button::kAchievementsPc:
      achievements_ = button == Button::kAchievementsPc ? settings::AchievementSet::kPc
                                                        : settings::AchievementSet::kXbox;
      page_ = Page::kReady;
      return Action::kSaveAchievements;
    case Button::kPlay:
      return Action::kPlay;
    case Button::kReinstall:
      error_ = {};
      page_ = Page::kInstall;
      return Action::kNone;
    case Button::kChangeAchievements:
      page_ = Page::kAchievements;
      return Action::kNone;
  }
  return Action::kNone;
}

Action LauncherModel::Close() {
  if (page_ != Page::kInstalling) return Action::kQuit;
  quit_after_install_ = true;
  if (cancelling_) return Action::kNone;
  cancelling_ = true;
  return Action::kCancelInstall;
}

Action LauncherModel::Picked(PickOutcome outcome, const std::string& path,
                             const std::string& error) {
  if (page_ != Page::kInstall && page_ != Page::kBrowse) return Action::kNone;
  switch (outcome) {
    case PickOutcome::kCancelled:
      page_ = Page::kInstall;
      return Action::kNone;
    case PickOutcome::kFailed:
      // From now on the buttons open the browser directly; the note says why once.
      picker_failed_ = true;
      note_ = {std::string(game_setup::kTextPickerFailed), {{"error", error}}};
      page_ = Page::kBrowse;
      return Action::kBrowse;
    case PickOutcome::kChosen:
      break;
  }
  path_ = path;
  error_ = {};
  phase_ = game_setup::Phase::kCopying;
  done_ = total_ = 0;
  cancelling_ = false;
  page_ = Page::kInstalling;
  return Action::kInstall;
}

void LauncherModel::Progress(game_setup::Phase phase, uint64_t done, uint64_t total) {
  phase_ = phase;
  done_ = done;
  total_ = total;
}

double LauncherModel::fraction() const {
  if (!total_) return 0;
  return std::min(1.0, double(done_) / double(total_));
}

Action LauncherModel::Finished(const game_setup::InstallResult& result) {
  if (page_ != Page::kInstalling) return Action::kNone;
  cancelling_ = false;
  if (result.error.empty()) {
    installed_ = true;
    if (quit_after_install_) return Action::kQuit;  // finished before the cancel took effect
    return AfterInstall();
  }
  page_ = Page::kInstall;
  if (!game_setup::Cancelled(result.error)) error_ = result.error;
  return quit_after_install_ ? Action::kQuit : Action::kNone;
}

Action LauncherModel::AfterInstall() {
  page_ = achievements_ ? Page::kReady : Page::kAchievements;
  return Action::kNone;
}

}  // namespace torchlight::launcher
