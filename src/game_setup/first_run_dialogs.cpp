// The first start on Android: the system's dialogs, a question at a time (desktop: the wizard).

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

#include "game_setup/first_run_steps.h"
#include "game_setup/game_files.h"
#include "game_setup/install.h"
#include "platform/platform.h"

namespace torchlight::game_setup {
namespace {

std::string Text(const Translate& tr, std::string_view english,
                 std::vector<std::pair<std::string, std::string>> values = {}) {
  return Render({std::string(english), std::move(values)}, tr);
}

// Runs Install on a thread while the progress window is drawn here; the window's close button
// cancels.
InstallResult InstallWithProgress(Source source, const std::filesystem::path& from,
                                  const std::filesystem::path& game_dir, const Translate& tr) {
  auto window = platform::ProgressWindow::Open(Text(tr, kTextTitle));
  std::atomic<bool> cancel{false}, finished{false};
  std::mutex mutex;
  Phase phase = Phase::kCopying;
  uint64_t done = 0, total = 0;
  InstallResult result;
  std::thread worker([&] {
    result = Install(source, from, game_dir, GameFiles(),
                     [&](Phase p, uint64_t d, uint64_t t) {
                       std::lock_guard lock(mutex);
                       phase = p, done = d, total = t;
                       return !cancel.load();
                     });
    finished = true;
  });
  const std::string copying =
      Text(tr, source == Source::kPackage ? kTextExtracting : kTextCopying);
  const std::string checking = Text(tr, kTextChecking);
  while (!finished) {
    double fraction = 0;
    bool is_checking = false;
    {
      std::lock_guard lock(mutex);
      if (total) fraction = double(done) / double(total);
      is_checking = phase == Phase::kChecking;
    }
    if (window && !window->Show(fraction, is_checking ? checking : copying)) cancel = true;
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
  }
  worker.join();
  return result;
}

GameData AskGame(const std::filesystem::path& game_dir, const Translate& tr,
                 std::vector<platform::StartupMessage>& log) {
  const auto info = [&](std::string text) { log.push_back({false, std::move(text)}); };
  const std::string title = Text(tr, kTextTitle);
  const std::string where = game_dir.string();
  // Where no folder can be picked (Android), the package is the only choice.
  const bool folders = platform::CanPickFolder();
  struct ForgetCopies {
    ~ForgetCopies() { platform::ForgetPickedFiles(); }
  } forget_copies;
  while (true) {
    const int choice =
        folders ? platform::AskChoice(title, Text(tr, kTextIntro, {{"where", where}}),
                                      {Text(tr, kTextChoosePackage), Text(tr, kTextChooseFolder),
                                       Text(tr, kTextQuit)})
                : platform::AskChoice(title, Text(tr, kTextIntroPackageOnly, {{"where", where}}),
                                      {Text(tr, kTextChoosePackage), Text(tr, kTextQuit)});
    if (choice != 0 && !(folders && choice == 1)) {
      info("game setup: no game files; quit by the user");
      return GameData::kQuit;
    }
    const Source source = choice == 0 ? Source::kPackage : Source::kFolder;
    std::string path, error;
    const platform::PickResult picked =
        source == Source::kPackage ? platform::PickFile(Text(tr, kTextPackagePicker), path, error)
                                   : platform::PickFolder(Text(tr, kTextFolderPicker), path, error);
    if (picked == platform::PickResult::kCancelled) continue;
    if (picked == platform::PickResult::kFailed) {
      platform::ShowError(title, Text(tr, kTextPickerFailed, {{"error", error}}));
      continue;
    }
    if (picked == platform::PickResult::kUncopied) {
      platform::ShowError(title, Text(tr, kTextPickedCopyFailed, {{"error", error}}));
      info("game setup: the chosen file was not copied: " + error);
      continue;
    }
    if (source == Source::kPackage) {
      const auto facts = ReadPackageFacts(path);
      const Message problem =
          facts ? CheckPackage(*facts) : Message{std::string(kTextNotPackage), {}};
      if (!problem.empty()) {
        platform::ShowError(title, Render(problem, tr));
        info("game setup: " + path + " rejected: " + Render(problem));
        continue;
      }
    }
    const InstallResult result = InstallWithProgress(source, path, game_dir, tr);
    if (Cancelled(result.error)) {
      info("game setup: install from " + path + " cancelled");
      continue;
    }
    if (!result.error.empty()) {
      platform::ShowError(title, Render(result.error, tr));
      log.push_back(
          {true, "game setup: install from " + path + " failed: " + Render(result.error)});
      continue;
    }
    if (!result.moved_to.empty()) {
      info("game setup: the previous " + where + " was moved to " + result.moved_to);
    }
    info("game setup: installed the game files from " + path + " to " + where);
    return GameData::kReady;
  }
}

}  // namespace

GameData AskFirstStart(const OpenQuestions& questions, const std::optional<StorageQuestion>&,
                       const std::function<OpenQuestions(bool)>&, const Translate& translate,
                       FirstStartAnswers& answers, std::vector<platform::StartupMessage>& log) {
  if (questions.ask_game && AskGame(questions.game_dir, translate, log) != GameData::kReady) {
    return GameData::kQuit;
  }
  if (questions.ask_achievements) {
    // Closing the box keeps the default, Xbox 360, and saves it too.
    const int choice = platform::AskChoice(
        Text(translate, kTextTitle), Text(translate, kTextAchievementsChoice),
        {Text(translate, kTextAchievementsXbox), Text(translate, kTextAchievementsPc)});
    answers.achievement = choice == 1 ? settings::AchievementSet::kPc : settings::AchievementSet::kXbox;
  }
  return GameData::kReady;
}

}  // namespace torchlight::game_setup
