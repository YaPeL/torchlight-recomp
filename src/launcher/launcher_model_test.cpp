// Tests for the launcher's model: which page a start opens on, each page's buttons, and what the
// buttons, the pickers and the install lead to (docs/launcher.md, stage 1).

#include <cstdio>
#include <cstdlib>
#include <vector>

#include "game_setup/game_files.h"
#include "launcher/launcher_model.h"

namespace {

using namespace torchlight::launcher;
using torchlight::game_setup::InstallResult;
using torchlight::game_setup::Message;
using torchlight::game_setup::Phase;
using torchlight::game_setup::Source;
using torchlight::settings::AchievementSet;

int failures = 0;
void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

InstallResult Done() { return {}; }
InstallResult Cancelled() {
  return {Message{std::string(torchlight::game_setup::kCancelled), {}}, {}};
}
InstallResult Failed() {
  return {Message{std::string(torchlight::game_setup::kTextCannotRead), {{"file", "pak.zip"}}}, {}};
}

void TestNeeded() {
  Check(!Needed({true, AchievementSet::kXbox, false}), "everything there: no launcher");
  Check(Needed({true, AchievementSet::kXbox, true}), "--launcher: launcher");
  Check(Needed({false, AchievementSet::kXbox, false}), "no game files: launcher");
  Check(Needed({true, std::nullopt, false}), "no achievement set: launcher");
}

void TestFirstStart() {
  LauncherModel m({false, std::nullopt, false});
  Check(m.page() == Page::kInstall, "first start: Install");
  Check(m.Buttons() == std::vector<Button>{Button::kChoosePackage, Button::kChooseFolder,
                                           Button::kQuit},
        "Install's buttons, Quit without a game");
  Check(m.Press(Button::kPlay) == Action::kNone, "a button of another page does nothing");
  Check(m.Press(Button::kChoosePackage) == Action::kPickPackage, "package: the system's picker");
  Check(m.Picked(PickOutcome::kCancelled, "") == Action::kNone && m.page() == Page::kInstall,
        "picker cancelled: back to Install");
  m.Press(Button::kChoosePackage);
  Check(m.Picked(PickOutcome::kChosen, "/games/LIVE") == Action::kInstall, "chosen: install");
  Check(m.page() == Page::kInstalling && m.install_source() == Source::kPackage &&
            m.install_path() == "/games/LIVE",
        "installing the chosen package");
  Check(m.fraction() == 0, "no total yet: 0");
  m.Progress(Phase::kCopying, 50, 200);
  Check(m.fraction() == 0.25 && m.phase() == Phase::kCopying, "progress");
  m.Progress(Phase::kChecking, 300, 200);
  Check(m.fraction() == 1 && m.phase() == Phase::kChecking, "progress kept at most 1");
  Check(m.Picked(PickOutcome::kChosen, "/other") == Action::kNone, "no pick while installing");
  Check(m.Finished(Done()) == Action::kNone && m.page() == Page::kAchievements,
        "installed: the achievement set next");
  Check(m.Buttons() == std::vector<Button>{Button::kAchievementsXbox, Button::kAchievementsPc},
        "first choice of the set: no way back");
  Check(m.Press(Button::kAchievementsPc) == Action::kSaveAchievements, "set saved");
  Check(m.achievements() == AchievementSet::kPc && m.page() == Page::kReady, "PC, then Ready");
  Check(m.Press(Button::kPlay) == Action::kPlay, "Play");
}

void TestInstallError() {
  LauncherModel m({false, AchievementSet::kXbox, false});
  m.Press(Button::kChooseFolder);
  Check(m.Picked(PickOutcome::kChosen, "/extracted") == Action::kInstall &&
            m.install_source() == Source::kFolder,
        "folder install");
  Check(m.Finished(Failed()) == Action::kNone && m.page() == Page::kInstall, "failed: Install");
  Check(m.error().text == torchlight::game_setup::kTextCannotRead, "the error is shown");
  Check(m.Press(Button::kChooseFolder) == Action::kPickFolder && m.error().empty(),
        "trying again clears it");
  m.Picked(PickOutcome::kChosen, "/extracted");
  Check(m.Finished(Done()) == Action::kNone && m.page() == Page::kReady,
        "installed with a saved set: Ready");
}

void TestCancel() {
  LauncherModel m({false, std::nullopt, false});
  m.Press(Button::kChoosePackage);
  m.Picked(PickOutcome::kChosen, "/games/LIVE");
  Check(m.Buttons() == std::vector<Button>{Button::kCancel}, "Installing: Cancel");
  Check(m.Press(Button::kCancel) == Action::kCancelInstall && m.cancelling(), "cancel asked");
  Check(m.Buttons().empty() && m.Press(Button::kCancel) == Action::kNone, "asked once");
  Check(m.Finished(Cancelled()) == Action::kNone && m.page() == Page::kInstall &&
            m.error().empty(),
        "cancelled: Install, no error");
}

void TestCloseWhileInstalling() {
  {
    LauncherModel m({false, std::nullopt, false});
    m.Press(Button::kChoosePackage);
    m.Picked(PickOutcome::kChosen, "/games/LIVE");
    Check(m.Close() == Action::kCancelInstall, "close while installing: cancel first");
    Check(m.Close() == Action::kNone, "a second close waits too");
    Check(m.Finished(Cancelled()) == Action::kQuit, "then quit");
  }
  {
    LauncherModel m({false, std::nullopt, false});
    m.Press(Button::kChoosePackage);
    m.Picked(PickOutcome::kChosen, "/games/LIVE");
    m.Close();
    Check(m.Finished(Done()) == Action::kQuit, "finished before the cancel: quit all the same");
  }
  LauncherModel m({false, std::nullopt, false});
  Check(m.Close() == Action::kQuit, "close elsewhere: quit");
}

void TestPickerFailed() {
  LauncherModel m({false, std::nullopt, false});
  m.Press(Button::kChoosePackage);
  Check(m.Picked(PickOutcome::kFailed, "", "no portal") == Action::kBrowse &&
            m.page() == Page::kBrowse && m.browse_source() == Source::kPackage,
        "no picker: the browser, for a package");
  Check(m.note().text == torchlight::game_setup::kTextPickerFailed &&
            m.note().values.size() == 1 && m.note().values[0].second == "no portal",
        "the note says why");
  Check(m.Buttons() == std::vector<Button>{Button::kBack}, "Browse: Back");
  Check(m.Press(Button::kBack) == Action::kNone && m.page() == Page::kInstall, "back to Install");
  Check(m.Press(Button::kChooseFolder) == Action::kBrowse && m.browse_source() == Source::kFolder,
        "after a failure the buttons open the browser directly");
  Check(m.Picked(PickOutcome::kChosen, "/extracted") == Action::kInstall &&
            m.install_source() == Source::kFolder,
        "chosen in the browser: install");
}

void TestOnDemand() {
  LauncherModel m({true, AchievementSet::kXbox, true});
  Check(m.page() == Page::kReady, "--launcher: Ready");
  Check(m.Buttons() == std::vector<Button>{Button::kPlay, Button::kChangeAchievements,
                                           Button::kReinstall, Button::kQuit},
        "Ready's buttons");
  Check(m.Press(Button::kChangeAchievements) == Action::kNone && m.page() == Page::kAchievements,
        "change the set");
  Check(m.Buttons() == std::vector<Button>{Button::kAchievementsXbox, Button::kAchievementsPc,
                                           Button::kBack},
        "a saved set: Back too");
  Check(m.Press(Button::kBack) == Action::kNone && m.page() == Page::kReady &&
            m.achievements() == AchievementSet::kXbox,
        "back unchanged");
  Check(m.Press(Button::kReinstall) == Action::kNone && m.page() == Page::kInstall,
        "reinstall: Install");
  Check(m.Buttons() == std::vector<Button>{Button::kChoosePackage, Button::kChooseFolder,
                                           Button::kBack},
        "with a game installed: Back instead of Quit");
  Check(m.Press(Button::kBack) == Action::kNone && m.page() == Page::kReady, "back to Ready");
  Check(m.Press(Button::kQuit) == Action::kQuit, "Quit");
}

void TestMissingAchievements() {
  LauncherModel m({true, std::nullopt, false});
  Check(m.page() == Page::kAchievements, "installed without a set: Achievements");
  Check(m.Buttons().size() == 2, "no way back");
  Check(m.Close() == Action::kQuit, "closing quits; nothing saved, asked again next time");
  Check(!m.achievements(), "no set");
  Check(m.Picked(PickOutcome::kChosen, "/x") == Action::kNone, "a stale pick does nothing");
}

}  // namespace

int main() {
  TestNeeded();
  TestFirstStart();
  TestInstallError();
  TestCancel();
  TestCloseWhileInstalling();
  TestPickerFailed();
  TestOnDemand();
  TestMissingAchievements();
  if (failures) return 1;
  std::printf("launcher model test: ok\n");
  return 0;
}
