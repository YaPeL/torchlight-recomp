// The first start (REL.4): when the game's files are not installed yet, ask for the user's
// Torchlight XBLA package or an extracted folder, check it and install it.

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "platform/user_folders.h"

namespace torchlight::game_setup {

enum class GameData {
  kReady,          // the game is there and nothing is left to ask
  kQuit,           // the player closed the setup: nothing more was written
  kMissingInRoot,  // no game, and this run may not ask: it says so and stops
  kFailed,         // the setup's window could not open: it says so and stops
};
// The first start's questions, those still open: the game's files in `game_dir` (empty: not asked;
// its default.xex checks out at every start, a cheap test), and with `ask_achievements` the
// achievement set when settings.toml has no "achievements" key yet. A run with --user_data_root and
// no --game_data_root does not ask for the game (`may_ask` false): nobody may be there to answer
// (test runs, scripts, a phone). `log` gets what was done, for the runtime's log (logging is not up
// yet). The wizard on desktop, the system's dialogs on Android.
GameData FirstStart(const std::filesystem::path& game_dir, bool may_ask, bool ask_achievements,
                    std::vector<platform::StartupMessage>& log);

// The renderer could not start (`error`, the render system's reason): a message box in the setup's
// language says so, instead of a black window. Main thread; the caller then closes the game.
void ShowRendererError(const std::string& error);

}  // namespace torchlight::game_setup
