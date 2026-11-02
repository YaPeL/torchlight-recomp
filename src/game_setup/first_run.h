// The first start (REL.4): when the game's files are not installed yet, ask for the user's
// Torchlight XBLA package or an extracted folder, check it and install it.

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "platform/storage_mode.h"
#include "platform/user_folders.h"

namespace torchlight::game_setup {

enum class GameData {
  kReady,          // the game is there and nothing is left to ask
  kQuit,           // the player closed the setup: nothing more was written
  kMissingInRoot,  // no game, and this run may not ask: it says so and stops
  kFailed,         // the setup's window could not open: it says so and stops
};
// What the start knows when it asks.
struct FirstStartFacts {
  platform::StorageMode storage = platform::StorageMode::kUserFolders;  // SetUpFolders' answer
  std::filesystem::path portable_folder;  // where a portable folder goes, when one is possible
  bool game_given = false;                // --game_data_root
  bool own_user_root = false;             // --user_data_root
};
// The first start's questions, those still open: where to keep the files when `storage` leaves it to
// the player, the game's files (their default.xex checks out at every start, a cheap test), and the
// achievement set when settings.toml has no "achievements" key yet. A run with --user_data_root and
// no --game_data_root does not ask for the game: nobody may be there to answer (test runs, scripts,
// a phone); nor for the achievement set. `log` gets what was done, for the runtime's log (logging is
// not up yet). The wizard on desktop, the system's dialogs on Android.
GameData FirstStart(const FirstStartFacts& facts, std::vector<platform::StartupMessage>& log);

// The renderer could not start (`error`, the render system's reason): a message box in the setup's
// language says so, instead of a black window. Main thread; the caller then closes the game.
void ShowRendererError(const std::string& error);

}  // namespace torchlight::game_setup
