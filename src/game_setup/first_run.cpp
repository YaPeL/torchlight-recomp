#include "game_setup/first_run.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <thread>

#include <rex/cvar.h>

#include "game_menu/menu_strings.h"
#include "game_setup/game_files.h"
#include "game_setup/install.h"
#include "game_setup/setup_text.h"
#include "platform/platform.h"
#include "settings/host_settings.h"

namespace torchlight::game_setup {

namespace {

constexpr const char* kStringsFile = "tl_setup_strings.txt";

}  // namespace

Translate SetupTranslate(std::vector<platform::StartupMessage>& log) {
  auto strings = std::make_shared<game_menu::MenuStrings>();
  std::vector<std::string> warnings;
  std::string error;
  const std::string exe_dir = platform::ExecutableDir();
  if (exe_dir.empty() ||
      !strings->Load(exe_dir + "data/ui/" + kStringsFile, warnings, error)) {
    log.push_back({true, "game setup: " + error + "; texts in English"});
  }
  for (const std::string& warning : warnings) log.push_back({true, "game setup: " + warning});
  settings::HostSettings host;
  if (const std::string dir = platform::ConfigDir(); !dir.empty()) {
    std::vector<std::string> ignored;
    std::string load_error;
    settings::Load(dir + std::string(settings::kFileName), host, ignored, load_error);
  }
  std::optional<std::string> console;
  if (const auto* flag = rex::cvar::GetFlagInfo("user_language");
      flag && flag->source == rex::cvar::Source::kCommandLine) {
    console = settings::LanguageCode(rex::cvar::Query<uint32_t>("user_language"));
  }
  const std::string language = SetupLanguage(host.language, console,
                                             platform::PreferredLanguages(), strings->Languages());
  log.push_back({false, "game setup: texts in language " + language});
  return [strings, language](const std::string& english) {
    return strings->Translate(language, english);
  };
}

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

}  // namespace

bool EnsureGameData(const std::filesystem::path& game_dir,
                    std::vector<platform::StartupMessage>& log) {
  const auto info = [&](std::string text) { log.push_back({false, std::move(text)}); };
  if (QuickCheckGameFolder(game_dir, GameFiles()).empty()) return true;
  const Translate tr = SetupTranslate(log);
  const std::string title = Text(tr, kTextTitle);
  const std::string where = game_dir.string();
  while (true) {
    const int choice = platform::AskChoice(
        title, Text(tr, kTextIntro, {{"where", where}}),
        {Text(tr, kTextChoosePackage), Text(tr, kTextChooseFolder), Text(tr, kTextQuit)});
    if (choice != 0 && choice != 1) {
      info("game setup: no game files; quit by the user");
      return false;
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
    return true;
  }
}

void EnsureAchievementChoice(std::vector<platform::StartupMessage>& log) {
  const std::string dir = platform::ConfigDir();
  if (dir.empty()) return;
  const std::string path = dir + std::string(settings::kFileName);
  settings::HostSettings host;
  std::vector<std::string> warnings;
  std::string error;
  if (!settings::Load(path, host, warnings, error)) {
    log.push_back({true, "achievements choice: " + error + "; not asked"});
    return;
  }
  if (host.achievements) return;  // chosen before
  const Translate tr = SetupTranslate(log);
  const int choice = platform::AskChoice(
      Text(tr, kTextTitle), Text(tr, kTextAchievementsChoice),
      {Text(tr, kTextAchievementsXbox), Text(tr, kTextAchievementsPc)});
  host.achievements = choice == 1 ? settings::AchievementSet::kPc : settings::AchievementSet::kXbox;
  if (!settings::Save(path, host, error)) {
    log.push_back({true, "achievements choice: cannot save " + path + ": " + error});
    return;
  }
  log.push_back({false, "achievements choice: " +
                            std::string(settings::AchievementSetName(*host.achievements)) +
                            (choice < 0 ? " (the box was closed: the default)" : "")});
}

}  // namespace torchlight::game_setup
