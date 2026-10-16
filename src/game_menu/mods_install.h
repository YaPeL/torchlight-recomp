// The player's PC mods (docs/mods.md): found in <data folder>/mods/ with PC's layout, served to the
// guest each through its own writable VFS device (tlmod<N>:, hooks/guest_path.h ModDeviceLink; the
// game compiles a mod's text .DAT into the mod's folder), and registered with the guest's own mod system once its data manager exists, with a mod
// manager the host builds (guest_abi/mods.h: the Xbox build has none). Without mods nothing is
// mounted or built and the guest behaves as the original game. The unit save writer is hooked here
// too: a character saved with mods records their names, and the Xbox writer's bug with that list
// (guest_abi/mods.h kUnitSaveWriter) is repaired as the save is written.

#pragma once

#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <string_view>

#include <rex/ppc/context.h>

#include "mods/mod_list.h"
#include "mods/pad_prompts.h"

namespace rex {
class Runtime;
}

namespace torchlight::game_menu {

// Before the guest runs (OnPostSetup), any mode: scans the mods folder, keeps mods.dat up to date,
// backs the saves up when the set of mods changed since the previous start (mods/save_safety.h),
// and mounts the folder when there are mods. `data_dir` is platform::DataDir() (mods/ and the
// record live there); `user_data_root` holds the saves; `game_pak` is the game's pak.zip (the names
// of its own CEGUI imagesets, GameImagesetNames).
void InstallMods(rex::Runtime* runtime, const std::filesystem::path& data_dir,
                 const std::filesystem::path& user_data_root, const std::filesystem::path& game_pak);

// Before the guest runs, after InstallMods, any mode: the pad prompts' style (mods/pad_prompts.h)
// from the setting (`chosen`, none for automatic) or the connected pad, its textures written to the
// cache and mounted like the replacements folder (before it), and confirm/cancel swapped in the
// guest's pad reads when `swap` applies to the style. Nothing is written for the Xbox style.
void InstallPadPrompts(rex::Runtime* runtime, const std::filesystem::path& game_pak,
                       std::optional<mods::PadFamily> chosen, bool swap);

// The data manager was just built (its constructor's hook, on the game's thread, before the
// global data loader checks the MODS achievements and loads the data): builds the mod manager,
// registers the mods in their planned order, and puts each active mod's folder in the mod's own
// resource group, initialised after the game's groups in mods.dat's order, as PC does. Nothing
// when there are no mods.
void RegisterMods(PPCContext& ctx, uint8_t* base, uint32_t data_manager);

// The data manager the resources.cfg loader is about to build (its hook, before the original): its
// group list is where the replacements folder goes before the groups are initialised.
void NoteDataManager(uint32_t data_manager);

// The plan InstallMods mounted (the mods the guest gets), or null when there are none.
const mods::ModPlan* MountedModPlan();

// The resource group RegisterMods put an active mod's folder in (`folder`: its name in the mods
// folder), or none (not active, or not registered yet).
std::optional<std::string> ModResourceGroup(std::string_view folder);

// The names of the game's own CEGUI imagesets (mods/class_list.h PakImagesetNames on the pak
// InstallMods was given), read the first time.
const std::set<std::string>& GameImagesetNames();

}  // namespace torchlight::game_menu
