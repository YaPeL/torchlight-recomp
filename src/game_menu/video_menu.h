// The host's video menu inside the game's own UI (guest_abi/game_ui.h): our layouts (data/ui/,
// installed next to the executable) are served to the guest through a VFS device and an OGRE
// resource location, and a column of video settings is added to the game's settings menu.
//
// Guest calls happen only in hooks, on the game's thread.

#pragma once

#include <filesystem>
#include <string>

namespace rex {
class Runtime;
}

namespace torchlight::game_menu {

class GuestCall;

// After the runtime is set up, before the guest runs (OnPostSetup): mounts the layouts and enables
// the hooks. Without it (or when the layouts are missing) the hooks only call the originals. On a
// frame wider than 16:9 it also serves the game's framed layouts with their bands re-anchored
// (wide_layout.h), generated from the user's pak into the local cache.
void Install(rex::Runtime* runtime, const std::filesystem::path& game_data_root);

// One OGRE FileSystem resource location in the default group, added through the guest (game's
// thread); false on failure. Also used for the player's mods (mods_install.h).
bool AddFileSystemLocation(GuestCall& call, const std::string& path, bool recursive,
                           const std::string& group = "General");

}  // namespace torchlight::game_menu
