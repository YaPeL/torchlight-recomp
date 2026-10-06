// The in-game import of Torchlight PC saves from the import/ folder of the game's files
// (platform/user_folders.h kGameData: ~/.local/share/TorchlightRecomp/game/import/ on Linux,
// %LOCALAPPDATA%\TorchlightRecomp\game\import\ on Windows; docs/saves-research.md, section 4d): the hooks on the "load character" menu and the start-up step. The conversion and the
// decisions live in src/save_import/.

#pragma once

#include <filesystem>

namespace rex {
class Runtime;
}

namespace torchlight::game_menu {

// Before the guest runs: applies the stash and settings confirmed in a previous session (they
// cannot be replaced while the game runs: it keeps them in memory and writes them back) and turns
// on the menu hooks. Without an import folder it only turns the hooks on. `game_data_root` is the
// game data in use (its pak.zip is the 360 data the saves are checked against).
// It also prunes the old save backups in <user_data_root>/save-backups/ (save_import/
// backup_retention.h).
void InstallSaveImport(rex::Runtime* runtime, const std::filesystem::path& game_data_root,
                       const std::filesystem::path& user_data_root);

}  // namespace torchlight::game_menu
