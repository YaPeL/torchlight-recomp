#include "game_setup/first_run.h"

#include <optional>

#include <rex/cvar.h>

#include "game_menu/menu_strings.h"
#include "game_setup/game_files.h"
#include "game_setup/first_run_steps.h"
#include "game_setup/setup_text.h"
#include "platform/platform.h"
#include "settings/host_settings.h"

namespace torchlight::game_setup {

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

std::string Text(const Translate& tr, std::string_view english,
                 std::vector<std::pair<std::string, std::string>> values = {}) {
  return Render({std::string(english), std::move(values)}, tr);
}

}  // namespace

GameData FirstStart(const std::filesystem::path& game_dir, bool may_ask, bool ask_achievements,
                    std::vector<platform::StartupMessage>& log) {
  const bool ask_game = !game_dir.empty() && !XexMatches(game_dir, GameFiles());
  if (ask_game && !may_ask) {
    platform::PrintStartupError("--user_data_root given, but no valid game in " + game_dir.string() +
                                ": pass --game_data_root=<the game's folder>, or put the game there");
    return GameData::kMissingInRoot;
  }
  // The achievement set, asked once: the settings have no "achievements" key yet.
  const std::string config = platform::ConfigDir();
  const std::string settings_path = config.empty() ? "" : config + std::string(settings::kFileName);
  settings::HostSettings host;
  bool needs_achievements = false;
  if (ask_achievements && !settings_path.empty()) {
    std::vector<std::string> warnings;
    std::string error;
    if (!settings::Load(settings_path, host, warnings, error)) {
      log.push_back({true, "achievements choice: " + error + "; not asked"});
    } else {
      needs_achievements = !host.achievements.has_value();
    }
  }
  if (!ask_game && !needs_achievements) return GameData::kReady;
  std::optional<settings::AchievementSet> achievement;
  const GameData answer = AskFirstStart(game_dir, ask_game, needs_achievements, SetupTranslate(log),
                                        achievement, log);
  if (answer == GameData::kReady && achievement) {
    host.achievements = achievement;
    std::string error;
    if (settings::Save(settings_path, host, error)) {
      log.push_back({false, "achievements choice: " +
                                std::string(settings::AchievementSetName(*achievement))});
    } else {
      log.push_back({true, "achievements choice: cannot save " + settings_path + ": " + error});
    }
  }
  return answer;
}

void ShowRendererError(const std::string& error) {
  std::vector<platform::StartupMessage> log;
  const Translate tr = SetupTranslate(log);
  platform::ShowError(Text(tr, kTextRendererTitle), Text(tr, kTextRendererFailed, {{"error", error}}));
}

}  // namespace torchlight::game_setup
