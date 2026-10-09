// The start of the game before the runtime (TorchlightApp::Create): whether the game's files and
// the achievement set are there, and the launcher when something is missing or --launcher asks
// for it (docs/launcher.md, stage 1). The decision is a pure function of what the start found
// (PlanStart, tested on its own); RunFirstStart gathers the facts and carries it out.

#pragma once

#include <optional>
#include <vector>

#include "game_setup/setup_text.h"
#include "launcher/launcher_model.h"
#include "platform/user_folders.h"
#include "settings/host_settings.h"

namespace torchlight::launcher {

// What the start found.
struct StartFacts {
  bool custom_game_dir = false;  // --game_data_root: not our folder to install into
  bool game_dir_known = true;    // there is a folder for the game's files at all
  game_setup::Message files;     // QuickCheckGameFolder of it: empty when the game is whole
  bool settings_read = true;     // settings.toml read (a missing file reads as empty)
  std::optional<settings::AchievementSet> achievements;  // its achievement set
  bool launcher_flag = false;    // --launcher
};

enum class StartStep {
  kGame,      // nothing to ask: start the game
  kLauncher,  // open the launcher with StartPlan::start
  kStop,      // --game_data_root is not a whole game: say so (StartFacts::files) and stop
};
struct StartPlan {
  StartStep step = StartStep::kGame;
  Start start;
};

// Without a folder for the game's files there is nothing to install (as before the launcher);
// with --game_data_root the files are checked but not installed; a settings.toml that cannot be
// read leaves the achievement set unasked (it could not be saved), Xbox 360 as everywhere else.
StartPlan PlanStart(const StartFacts& facts);

enum class FirstStart { kPlay, kQuit, kFailed };

// Gathers the facts (the game's folder, settings.toml, --launcher), then: the game; the launcher
// (Play or Quit); or the error for a --game_data_root that is not a whole game. When the
// launcher's window cannot open, the first start's dialogs (game_setup/first_run.h) instead. `log`
// gets what was done, for the runtime's log (logging is not up yet).
FirstStart RunFirstStart(std::vector<platform::StartupMessage>& log);

}  // namespace torchlight::launcher
