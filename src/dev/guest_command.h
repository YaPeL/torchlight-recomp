// Development only: --dev_guest_command="CMD;CMD & CMD..." runs guest developer commands (the game's own
// console executor, e.g. "FAME 1000", "DESCEND") to reach states that are slow to reach by playing,
// for validation runs. Compiled in only with the CMake option TORCHLIGHT_DEV_COMMANDS (off by default
// and never set by the release workflows); other builds ignore the flag and log that.
//
// One step (command_list.h: ';' separates steps, '&' the commands of a step) runs after each game
// level load, once the game is in its in-game state and no loading screen covers it, from the game
// UI's update on the game's thread (as the game runs its console).

#pragma once

#include <cstdint>

#include <rex/ppc/context.h>

namespace torchlight::dev {

// A game level finished loading (achievements' level-load hook): the next command may run.
void OnGameLevelLoaded();
// CGameUI::Update (achievements' toast hook), after the original: runs the next command when due.
void OnGameUiUpdate(PPCContext& ctx, uint8_t* base, uint32_t game_ui, uint32_t context);

}  // namespace torchlight::dev
