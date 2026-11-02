// The first start on desktop: one window (setup_pages.h).

#include <rex/cvar.h>

#include "game_setup/first_run_steps.h"
#include "game_setup/game_files.h"
#include "game_setup/setup_pages.h"
#include "platform/platform.h"

#ifdef TORCHLIGHT_DEV_INPUT
// Development only, to play the first start with nobody there (docs/dev-input-script.md). A release
// build has none of these.
REXCVAR_DEFINE_STRING(dev_setup_script, "", "Torchlight",
                      "Plays the setup wizard from this script");
REXCVAR_DEFINE_STRING(dev_setup_shots, "", "Torchlight",
                      "The folder for the screenshots of --dev_setup_script");
REXCVAR_DEFINE_STRING(dev_setup_browse_from, "", "Torchlight",
                      "The folder browser's first folder under --dev_setup_script");
#endif

namespace torchlight::game_setup {

GameData AskFirstStart(const std::filesystem::path& game_dir, bool ask_game, bool ask_achievements,
                       const Translate& translate,
                       std::optional<settings::AchievementSet>& achievement,
                       std::vector<platform::StartupMessage>& log) {
  SetupConfig config;
  config.game_dir = game_dir;
  config.files = GameFiles();
  config.ask_game = ask_game;
  config.ask_achievements = ask_achievements;
  config.translate = translate;
  wizard::Options options;
  options.title = Render({std::string(kTextRendererTitle), {}}, translate);
#ifdef TORCHLIGHT_DEV_INPUT
  options.script = REXCVAR_GET(dev_setup_script);
  options.shots = REXCVAR_GET(dev_setup_shots);
  if (!options.script.empty()) {
    if (REXCVAR_GET(dev_setup_browse_from).empty()) {
      // A script's run never lists the real home folder.
      platform::PrintStartupError("--dev_setup_script needs --dev_setup_browse_from");
      return GameData::kFailed;
    }
    options.scale = 1;
    config.own_browser_only = true;
    const std::string from = REXCVAR_GET(dev_setup_browse_from);
    config.browse_from = std::filesystem::path(std::u8string(from.begin(), from.end()));
  }
#endif
  SetupPages pages(std::move(config));
  const int status = wizard::Run(options, pages);
  log.insert(log.end(), pages.log().begin(), pages.log().end());
  // A development script's failure ends the run; only no window at all has another way on.
  if (status != 0 && (status != wizard::kNoWindow || !options.script.empty())) {
    platform::PrintStartupError("the setup script failed (status " + std::to_string(status) + ")");
    return GameData::kFailed;
  }
  if (status == wizard::kNoWindow) {
    // Only the achievement set was open: the default, as a closed box used to give, not worth
    // stopping a game that is there.
    if (!ask_game) {
      log.push_back({true, "game setup: no setup window; the default achievement set"});
      achievement = settings::AchievementSet::kXbox;
      return GameData::kReady;
    }
    const std::string reason = Render({std::string(kTextNoWindow), {}}, translate);
    platform::PrintStartupError(reason);
    platform::ShowError(options.title, reason);  // a player with no console
    return GameData::kFailed;
  }
  if (!pages.completed()) {
    log.push_back({false, "game setup: closed by the user"});
    return GameData::kQuit;
  }
  achievement = pages.achievements();
  return GameData::kReady;
}

}  // namespace torchlight::game_setup
