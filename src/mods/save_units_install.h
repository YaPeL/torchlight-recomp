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

// `plan` is the mounted plan (none without mods); `pak` the game's pak.zip. Without mods' units
// the saves are checked now against the Xbox index; with them, the check waits for the index the
// game will load (CheckSavesAgainstLoadedIndex), since a mod's unit may not get into it.
void InstallSaveUnits(const std::filesystem::path& data_dir, const std::filesystem::path& pak,
                      const std::filesystem::path& user_data_root, const ModPlan* plan);

// The deferred check, once, against the index the game is about to load (mods/
// unit_index_install.cpp, before the loader runs; the saves are not open yet). None: that index
// could not be read, nothing is changed.
void CheckSavesAgainstLoadedIndex(const std::optional<UnitIndex>& loaded);
bool SavesAwaitLoadedIndex();

// What the last InstallSaveUnits changed, when it changed anything (for the notice).
const std::optional<SaveUnitsReport>& SaveUnitsChanges();

}  // namespace torchlight::mods
