// What first_run.cpp hands over to the way each platform asks: the wizard on desktop
// (first_run_wizard.cpp), the system's dialogs on Android (first_run_dialogs.cpp).

#pragma once

#include <filesystem>
#include <optional>
#include <vector>

#include "game_setup/first_run.h"
#include "game_setup/setup_text.h"
#include "settings/host_settings.h"

namespace torchlight::game_setup {

// `achievement` gets the player's choice when one was asked and answered.
GameData AskFirstStart(const std::filesystem::path& game_dir, bool ask_game, bool ask_achievements,
                       const Translate& translate,
                       std::optional<settings::AchievementSet>& achievement,
                       std::vector<platform::StartupMessage>& log);

}  // namespace torchlight::game_setup
