// The host's video menu inside the game's own UI (guest_abi/game_ui.h): our layouts (data/ui/,
// installed next to the executable) are served to the guest through a VFS device and an OGRE
// resource location, and a column of video settings is added to the game's settings menu.
//
// Guest calls happen only in hooks, on the game's thread.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

#include <rex/ppc/context.h>

namespace rex {
class Runtime;
namespace ui {
class WindowedAppContext;
}  // namespace ui
}  // namespace rex

namespace torchlight::game_menu {

class GuestCall;

// After the runtime is set up, before the guest runs (OnPostSetup): mounts the layouts and enables
// the hooks. Without it (or when the layouts are missing) the hooks only call the originals. On a
// frame wider than 16:9 it also serves the game's framed layouts with their bands re-anchored
// (wide_layout.h), generated from the user's pak into the local cache.
// `app_context`: the UI thread, where Import Mod shows the system's pickers. `request_close`: closes
// the game as the player would (Quit Now after a mod was replaced).
void Install(rex::Runtime* runtime, const std::filesystem::path& game_data_root,
             rex::ui::WindowedAppContext* app_context, std::function<void()> request_close = {});

// Every frame of the game UI (CGameUI::Update, achievements/toast.cpp), game's thread: shows the
// result of an Import Mod once it is ready, with the game's message box.
void OnGameUiFrame(PPCContext& ctx, uint8_t* base);

// One OGRE FileSystem resource location in the default group, added through the guest (game's
// thread); false on failure. Also used for the player's mods (mods_install.h).
bool AddFileSystemLocation(GuestCall& call, const std::string& path, bool recursive,
                           const std::string& group = "General");

// Initialises an OGRE resource group through the guest (game's thread), which parses its scripts
// (materials and the rest); false when it could not be called.
bool InitialiseResourceGroup(GuestCall& call, const std::string& group);

}  // namespace torchlight::game_menu
