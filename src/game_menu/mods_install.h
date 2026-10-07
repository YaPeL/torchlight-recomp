// The player's PC mods (docs/mods.md): found in <data folder>/mods/ with PC's layout, served to the
// guest through a writable VFS device (tlmods:, the game compiles a mod's text .DAT into the mod's
// folder), and registered with the guest's own mod system once its data manager exists, with a mod
// manager the host builds (guest_abi/mods.h: the Xbox build has none). Without mods nothing is
// mounted or built and the guest behaves as the original game. The unit save writer is hooked here
// too: a character saved with mods records their names, and the Xbox writer's bug with that list
// (guest_abi/mods.h kUnitSaveWriter) is repaired as the save is written.

#pragma once

#include <filesystem>

#include <rex/ppc/context.h>

namespace rex {
class Runtime;
}

namespace torchlight::game_menu {

// Before the guest runs (OnPostSetup), any mode: scans the mods folder, keeps mods.dat up to date,
// backs the saves up when the set of mods changed since the previous start (mods/save_safety.h),
// and mounts the folder when there are mods. `data_dir` is platform::DataDir() (mods/ and the
// record live there); `user_data_root` holds the saves.
void InstallMods(rex::Runtime* runtime, const std::filesystem::path& data_dir,
                 const std::filesystem::path& user_data_root);

// The data manager was just built (its constructor's hook, on the game's thread, before the
// global data loader checks the MODS achievements and loads the data): builds the mod manager,
// registers the mods in their planned order and adds their folders as resource locations after the
// game's (the last location added wins). Nothing when there are no mods.
void RegisterMods(PPCContext& ctx, uint8_t* base, uint32_t data_manager);

}  // namespace torchlight::game_menu
