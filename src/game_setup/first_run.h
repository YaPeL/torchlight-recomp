// The first start (REL.4): when the game's files are not installed yet, ask for the user's
// Torchlight XBLA package or an extracted folder, check it and install it, with the system's
// dialogs and a progress window (platform.h). English texts for now.

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "platform/user_folders.h"

namespace torchlight::game_setup {

// True when `game_dir` holds the game (its default.xex checks out, a cheap test done at every
// start), installing it first if needed; false when the user quit instead. `log` gets what was
// done, for the runtime's log (logging is not up yet).
bool EnsureGameData(const std::filesystem::path& game_dir,
                    std::vector<platform::StartupMessage>& log);

// The achievement set (settings::AchievementSet), asked once: when settings.toml has no
// "achievements" key yet, a message box offers Xbox 360 and PC (incomplete), and the choice is saved
// there (closing the box keeps the default, Xbox 360, and saves it too). Before the host settings
// are read for the rest of the startup.
void EnsureAchievementChoice(std::vector<platform::StartupMessage>& log);

}  // namespace torchlight::game_setup
