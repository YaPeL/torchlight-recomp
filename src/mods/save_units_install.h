// At every start, before the guest runs: the saves' items and creatures whose units the game will
// not know are taken out (save_units.h), with the saves backed up first; the player is told on
// screen what was removed and where the copy is (the notice comes once the game's UI is up).
//
// Part of the executable (logging); the checks are torchlight_mods.

#pragma once

#include <filesystem>
#include <optional>

#include "mods/mod_list.h"
#include "mods/save_units.h"

namespace torchlight::mods {

// `plan` is the mounted plan (none without mods); `pak` the game's pak.zip.
void InstallSaveUnits(const std::filesystem::path& data_dir, const std::filesystem::path& pak,
                      const std::filesystem::path& user_data_root, const ModPlan* plan);

// What the last InstallSaveUnits changed, when it changed anything (for the notice).
const std::optional<SaveUnitsReport>& SaveUnitsChanges();

}  // namespace torchlight::mods
