#include "launcher/first_start.h"

#include <filesystem>
#include <string>

#include <rex/cvar.h>

#include "game_setup/first_run.h"
#include "game_setup/game_files.h"
#include "launcher/launcher.h"
#include "platform/platform.h"

REXCVAR_DEFINE_BOOL(launcher, false, "Torchlight",
                    "Open the launcher before the game, also when nothing is missing");

namespace torchlight::launcher {

namespace {

std::string Utf8(const std::filesystem::path& path) {
  const std::u8string text = path.u8string();
  return std::string(text.begin(), text.end());
}

}  // namespace

FirstStart RunFirstStart(std::vector<platform::StartupMessage>& log) {
  StartFacts facts;
  const std::string custom = rex::cvar::Query<std::string>("game_data_root");
  facts.custom_game_dir = !custom.empty();
  const std::filesystem::path game_dir =
      facts.custom_game_dir ? std::filesystem::path(custom)
                            : std::filesystem::path(platform::GameDataDir());
  facts.game_dir_known = !game_dir.empty();
  if (facts.game_dir_known) {
    facts.files = game_setup::QuickCheckGameFolder(game_dir, game_setup::GameFiles());
  }
  settings::HostSettings host;
  if (const std::string config = platform::ConfigDir(); !config.empty()) {
    std::vector<std::string> warnings;
    std::string error;
    facts.settings_read =
        settings::Load(config + std::string(settings::kFileName), host, warnings, error);
    if (!facts.settings_read) {
      log.push_back({true, "launcher: " + error + "; the achievement set is not asked"});
    }
  }
  facts.achievements = host.achievements;
  facts.launcher_flag = rex::cvar::Query<bool>("launcher");

  const StartPlan plan = PlanStart(facts);
  if (!facts.files.empty() && facts.game_dir_known) {
    log.push_back({false, "launcher: the game files in " + Utf8(game_dir) +
                              " are not whole: " + game_setup::Render(facts.files)});
  }
  switch (plan.step) {
    case StartStep::kGame:
      return FirstStart::kPlay;
    case StartStep::kStop: {
      const game_setup::Translate tr = game_setup::SetupTranslate(log);
      platform::ShowError(game_setup::Render({std::string(game_setup::kTextTitle), {}}, tr),
                          game_setup::Render(facts.files, tr));
      log.push_back({true, "launcher: --game_data_root is not a whole game; stopped"});
      return FirstStart::kFailed;
    }
    case StartStep::kLauncher:
      break;
  }

  const game_setup::Translate tr = game_setup::SetupTranslate(log);
  std::string error;
  const auto outcome =
      RunLauncher(plan.start, SystemServices(game_dir, tr), tr, Utf8(game_dir), log, error);
  if (outcome) {
    if (*outcome == Outcome::kQuit) log.push_back({false, "launcher: quit by the user"});
    return *outcome == Outcome::kPlay ? FirstStart::kPlay : FirstStart::kQuit;
  }
  // No launcher window: the first start's dialogs for what is missing.
  log.push_back({true, "launcher: its window cannot open (" + error + "); the setup dialogs instead"});
  if (!plan.start.game_installed && !game_setup::EnsureGameData(game_dir, log)) {
    return FirstStart::kQuit;
  }
  if (!plan.start.achievements) game_setup::EnsureAchievementChoice(log);
  return FirstStart::kPlay;
}

}  // namespace torchlight::launcher
