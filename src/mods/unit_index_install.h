// The player's mods' units in the game's unit index (docs/mods.md, section 7e; guest_abi/
// unit_index.h). The Xbox build loads its index ready-made from MEDIA/UNITDATA.RAW, so a mod's new
// units would never be spawned, dropped or found in a save. When mods bring unit definitions, the
// host builds an index from the Xbox one plus those units, each read by the game's own unit loading
// and property reads, merged the way PC Torchlight builds its table (unit_index.h), cached in the
// user's folder (unit_cache.h) and loaded by the game in place of the Xbox file. Without mods'
// units nothing changes.
//
// Part of the executable (guest calls); the file format, merge and cache are torchlight_mods.

#pragma once

#include <filesystem>

#include "mods/mod_list.h"

namespace rex {
class Runtime;
}

namespace torchlight::mods {

// Before the guest runs, after the mods are mounted (game_menu/mods_install.h): finds the mods'
// unit definitions, the cache key and a usable cached index, and mounts the cache folder for the
// game. `plan` is the mounted plan (none: no mods), `pak` the game's pak.zip.
void InstallUnitIndex(rex::Runtime* runtime, const std::filesystem::path& data_dir,
                      const std::filesystem::path& pak, const ModPlan* plan);

}  // namespace torchlight::mods
