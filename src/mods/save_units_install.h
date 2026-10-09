// At every start, before the guest runs: the saves' items and creatures whose units the game will
// not know are taken out (save_units.h), with the saves backed up first; the player is told on
// screen what was removed and where the copy is (the notice comes once the game's UI is up).
//
// The check runs before the guest because nothing then can be reading or writing a save. With mods'
// units it counts on the index the game is expected to load: the cached one when it exists,
// otherwise the base plus the GUIDs the mods' definitions set. Once the game has loaded its index,
// CompareWithLoadedUnits only compares: units the check counted on that the game does not hold are
// logged and told on screen; the saves are not changed again.
//
// Part of the executable (logging); the checks are torchlight_mods.

#pragma once

#include <filesystem>
#include <optional>
#include <unordered_set>

#include "mods/mod_list.h"
#include "mods/save_units.h"

namespace torchlight::mods {

// `plan` is the mounted plan (none without mods); `pak` the game's pak.zip.
void InstallSaveUnits(const std::filesystem::path& data_dir, const std::filesystem::path& pak,
                      const std::filesystem::path& user_data_root, const ModPlan* plan);

// After the game loaded its unit index with the mods' units (mods/unit_index_install.cpp): the
// GUIDs it holds, or none when that is not known (it loaded part of a file); then every mods' unit
// the check counted on is taken as missing.
void CompareWithLoadedUnits(const std::optional<std::unordered_set<int64_t>>& loaded);

// What the start changed or found, when there is anything to tell (for the notice).
const std::optional<SaveUnitsReport>& SaveUnitsChanges();

}  // namespace torchlight::mods
