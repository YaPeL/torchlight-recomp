#include "game_setup/first_run.h"

#include <fstream>
#include <optional>
#include <system_error>

#include <rex/cvar.h>

#include "game_menu/menu_strings.h"
#include "game_setup/game_files.h"
#include "game_setup/first_run_steps.h"
#include "game_setup/setup_text.h"
#include "platform/platform.h"
#include "platform/portable_copy.h"
#include "platform/storage_mode.h"
#include "settings/host_settings.h"

namespace torchlight::game_setup {

namespace fs = std::filesystem;

namespace {

constexpr const char* kStringsFile = "tl_setup_strings.txt";

// The texts in the language SetupLanguage picks (the host settings and the command line are read
// directly: the first start runs before the rest of the startup).
Translate SetupTranslate(std::vector<platform::StartupMessage>& log) {
  auto strings = std::make_shared<game_menu::MenuStrings>();
  std::vector<std::string> warnings;
  std::string error;
  const std::string resources = platform::ResourceDir();
  if (resources.empty() ||
      !strings->Load(resources + "data/ui/" + kStringsFile, warnings, error)) {
    log.push_back({true, "game setup: " + error + "; texts in English"});
  }
  for (const std::string& warning : warnings) log.push_back({true, "game setup: " + warning});
  settings::HostSettings host;
  // UserFolder, which creates nothing: the storage question may still be open.
  if (const fs::path dir = platform::UserFolder(platform::UserFolderKind::kConfig); !dir.empty()) {
    std::vector<std::string> ignored;
    std::string load_error;
    settings::Load((dir / settings::kFileName).string(), host, ignored, load_error);
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

std::string Text(const Translate& tr, std::string_view english,
                 std::vector<std::pair<std::string, std::string>> values = {}) {
  return Render({std::string(english), std::move(values)}, tr);
}

}  // namespace

GameData FirstStart(const FirstStartFacts& facts, std::vector<platform::StartupMessage>& log) {
  // Nothing is created until an answer needs it (a copy or an install): the player may still close
  // the window, and what a copy or an install finished then stays, for the next start.
  settings::HostSettings host;
  const auto open_questions = [&] {
    OpenQuestions open;
    if (!facts.game_given) {
      open.game_dir = platform::UserFolder(platform::UserFolderKind::kGameData);
      open.ask_game = !open.game_dir.empty() && !XexMatches(open.game_dir, GameFiles());
    }
    const fs::path config = platform::UserFolder(platform::UserFolderKind::kConfig);
    if (!facts.own_user_root && !config.empty()) {
      std::vector<std::string> warnings;
      std::string error;
      if (!settings::Load((config / settings::kFileName).string(), host, warnings, error)) {
        log.push_back({true, "achievements choice: " + error + "; not asked"});
      } else {
        open.ask_achievements = !host.achievements.has_value();
      }
    }
    return open;
  };
  std::optional<StorageQuestion> storage;
  if (facts.storage == platform::StorageMode::kAsk ||
      facts.storage == platform::StorageMode::kPortableUnwritable) {
    StorageQuestion question;
    question.mode = facts.storage;
    question.portable_folder = facts.portable_folder;
    std::string ignored;
    question.parent_writable = platform::CanWriteFolder(facts.portable_folder.parent_path(), ignored);
    question.user_sources = platform::UserFolderSources();
    question.user_data = platform::SourcesHoldFiles(question.user_sources);
    storage = question;
  }
  OpenQuestions open;
  if (!storage) {
    open = open_questions();
    if (open.ask_game && facts.own_user_root) {
      platform::PrintStartupError("--user_data_root given, but no valid game in " +
                                  open.game_dir.string() +
                                  ": pass --game_data_root=<the game's folder>, or put the game there");
      return GameData::kMissingInRoot;
    }
    if (!open.ask_game && !open.ask_achievements) return GameData::kReady;
  }
  const auto after_storage = [&](bool portable) {
    if (portable) {
      platform::SetPortableRoot(facts.portable_folder);
    } else {
      platform::SetPortableRoot({});
      const auto migrated = platform::MigrateUserFolders(false);
      log.insert(log.end(), migrated.begin(), migrated.end());
    }
    return open_questions();
  };
  FirstStartAnswers answers;
  const GameData answer =
      AskFirstStart(open, storage, after_storage, SetupTranslate(log), answers, log);
  if (answer != GameData::kReady) return answer;
  if (answers.portable) {
    std::error_code ec;
    if (*answers.portable) {
      fs::create_directories(facts.portable_folder, ec);
      if (ec) {
        platform::PrintStartupError("cannot create the portable folder: " + ec.message());
        return GameData::kFailed;
      }
      log.push_back({false, "storage: portable, " + facts.portable_folder.string()});
    } else {
      log.push_back({false, "storage: the user's folders, by the player's choice"});
      // Asked once. A portable folder that exists but cannot be written is asked again every start:
      // it may work next time, and then it is the player's again.
      if (facts.storage == platform::StorageMode::kAsk) {
        const fs::path config = platform::PlainUserFolder(platform::UserFolderKind::kConfig);
        fs::create_directories(config, ec);
        std::ofstream choice(config / platform::kUserFoldersChoiceFile);
        choice.close();
        if (config.empty() || ec || !choice) {
          log.push_back({true, "storage: cannot save the choice; it will be asked again"});
        }
      }
    }
  }
  if (answers.achievement) {
    const std::string dir = platform::ConfigDir();
    if (dir.empty()) {
      log.push_back({true, "achievements choice: no settings folder; not saved"});
      return GameData::kReady;
    }
    const std::string path = dir + std::string(settings::kFileName);
    host.achievements = answers.achievement;
    std::string error;
    if (settings::Save(path, host, error)) {
      log.push_back({false, "achievements choice: " +
                                std::string(settings::AchievementSetName(*answers.achievement))});
    } else {
      log.push_back({true, "achievements choice: cannot save " + path + ": " + error});
    }
  }
  return GameData::kReady;
}

void ShowRendererError(const std::string& error) {
  std::vector<platform::StartupMessage> log;
  const Translate tr = SetupTranslate(log);
  platform::ShowError(Text(tr, kTextRendererTitle), Text(tr, kTextRendererFailed, {{"error", error}}));
}

}  // namespace torchlight::game_setup
