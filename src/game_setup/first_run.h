// The first start (REL.4) with the system's dialogs: when the game's files are not installed yet,
// ask for the user's Torchlight XBLA package or an extracted folder, check it and install it, with
// a progress window (platform.h); then the achievement set. The launcher (launcher/first_start.h)
// does all this in its own window; these dialogs remain for when that window cannot open.

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "game_setup/setup_text.h"
#include "platform/user_folders.h"

namespace torchlight::game_setup {

// The first start's texts (tl_setup_strings.txt) in the language SetupLanguage picks: the host
// settings', then --user_language, then the system's. Read directly: the first start runs before
// the rest of the startup. `log` gets which language, and any problem with the file.
Translate SetupTranslate(std::vector<platform::StartupMessage>& log);

// True when `game_dir` holds the game (QuickCheckGameFolder, a cheap test done at every start),
// installing it first if needed; false when the user quit instead. `log` gets what was done, for
// the runtime's log (logging is not up yet).
bool EnsureGameData(const std::filesystem::path& game_dir,
                    std::vector<platform::StartupMessage>& log);

// The achievement set (settings::AchievementSet), asked once: when settings.toml has no
// "achievements" key yet, a message box offers Xbox 360 and PC (incomplete), and the choice is saved
// there (closing the box keeps the default, Xbox 360, and saves it too). Before the host settings
// are read for the rest of the startup.
void EnsureAchievementChoice(std::vector<platform::StartupMessage>& log);

}  // namespace torchlight::game_setup
