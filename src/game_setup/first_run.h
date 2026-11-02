// The first start (REL.4): when the game's files are not installed yet, ask for the user's
// Torchlight XBLA package or an extracted folder, check it and install it, with the system's
// dialogs and a progress window (platform.h). English texts for now.

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "platform/user_folders.h"

namespace torchlight::game_setup {

enum class GameData {
  kReady,          // `game_dir` holds the game
  kQuit,           // the user quit the setup
  kMissingInRoot,  // not there, and this run may not ask: it says so on stderr and stops
};
// Whether `game_dir` holds the game (its default.xex checks out, a cheap test done at every start),
// installing it first if needed. A run with --user_data_root and no --game_data_root does not ask
// (`may_ask` false): nobody may be there to answer (test runs, scripts, a phone), and installing is
// a question for a player, not for a test root. `log` gets what was done, for the runtime's log
// (logging is not up yet).
GameData EnsureGameData(const std::filesystem::path& game_dir, bool may_ask,
                        std::vector<platform::StartupMessage>& log);

// The achievement set (settings::AchievementSet), asked once: when settings.toml has no
// "achievements" key yet, a message box offers Xbox 360 and PC, and the choice is saved
// there (closing the box keeps the default, Xbox 360, and saves it too). Before the host settings
// are read for the rest of the startup.
void EnsureAchievementChoice(std::vector<platform::StartupMessage>& log);

// The renderer could not start (`error`, the render system's reason): a message box in the setup's
// language says so, instead of a black window. Main thread; the caller then closes the game.
void ShowRendererError(const std::string& error);

}  // namespace torchlight::game_setup
