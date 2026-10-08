// The game's lookup of a mod's file, made letter-case-insensitive (mod_files.h; guest_abi/mods.h
// kModFileLookup): when the game asks for a file with another case or separators than the mod's
// own spelling on disk, the lookup is asked again with that spelling. Without mods nothing changes.
//
// Part of the executable (guest calls); the table is torchlight_mods.

#pragma once

#include <filesystem>

#include "mods/mod_list.h"

namespace torchlight::mods {

// Before the guest runs, after the mods are mounted: the enabled mods' files (`plan` is the
// mounted plan, none without mods; `mods_folder` holds them).
void InstallModFiles(const std::filesystem::path& mods_folder, const ModPlan* plan);

}  // namespace torchlight::mods
