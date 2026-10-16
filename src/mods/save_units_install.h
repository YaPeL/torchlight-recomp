// At every start, before the guest runs: the saves' items and creatures whose units the game will
// not know are taken out (save_units.h), with the saves backed up first; the player is told on
// screen what was removed and where the copy is (the notice comes once the game's UI is up).
//
// The check runs before the guest because nothing then can be reading or writing a save. With mods'
// units it counts on the index the game is expected to load: the cached one when it exists,
// otherwise the base plus the GUIDs the mods' definitions set. Once the game has loaded its index,
// CompareWithLoadedUnits only compares: units the check counted on that the game does not hold are
// logged and told on screen; the saves are not changed again. When saves hold such units, they
// are copied and saving is off for the session (SavingBlocked).
//
// Part of the executable (logging); the checks are torchlight_mods.

#pragma once

#include <filesystem>
#include <optional>
#include <unordered_set>
#include <vector>

#include "mods/mod_list.h"
#include "mods/save_units.h"
#include "mods/unit_cache.h"

namespace torchlight::mods {

// `plan` is the mounted plan (none without mods) and `files` its unit definitions (unit_cache.h
// ScanModUnitFiles); `pak` the game's pak.zip; `data_dir` holds mods/ and `cache_dir` the unit
// index cache's unitdata/ (settings/user_folders_in_use.h data, cache).
// With mods' units and `index_built_in_game` (no cached index: the game's loader builds it,
// unit_index_install.h UnitIndexBuiltInGame), the check is left for CheckDeferredSaves: the units
// to expect then come from the index just built instead of every mod's definition read here, with
// nothing drawn yet (EE: about 2000 files opened, seconds on Windows).
void InstallSaveUnits(const std::filesystem::path& data_dir, const std::vector<ModUnitFile>& files,
                      const std::filesystem::path& cache_dir, const std::filesystem::path& pak,
                      const std::filesystem::path& user_data_root, const ModPlan* plan, bool index_built_in_game);

// The game's thread, in the unit index loader once the index is built and before the game loads
// it: the check InstallSaveUnits left, if any. No character is loaded before the index (the
// game loads it with its data), so the saves are checked before anything reads them, as at the
// start; nor is the save content mounted then (the game's SAVE:\ lookups during its data load
// find no device; EE run, 2026-10-16), so the check's backups and rewrites meet no open file.
// While a check is left, saving is off (SavingBlocked): if the loader never ran it, nothing is
// written unchecked.
void CheckDeferredSaves();
// Whether a check InstallSaveUnits left has not run yet. Any thread.
bool SaveCheckPending();

// After the game loaded its unit index with the mods' units (mods/unit_index_install.cpp): the
// GUIDs it holds, or none when that is not known (it loaded part of a file); then every mods' unit
// the check counted on is taken as missing.
void CompareWithLoadedUnits(const std::optional<std::unordered_set<int64_t>>& loaded);

// True once CompareWithLoadedUnits found saves holding units the game did not load: the game would
// drop those items when it loads such a character and lose them at the next save, so nothing is
// saved for the rest of the session (save_block_hooks.cpp); also while the saves' check is left
// for later (SaveCheckPending). Any thread.
bool SavingBlocked();

// What the start changed or found, when there is anything to tell (for the notice).
const std::optional<SaveUnitsReport>& SaveUnitsChanges();

}  // namespace torchlight::mods
