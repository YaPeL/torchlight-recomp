// The player's notice for what ProtectSaves did (save_units.h): which saved characters lost how
// many items, where the copy from before is, which saves were left unchanged although they hold
// unknown items, and how many mods' units the game could not load after the saves were checked. Texts translated by the import messages' strings
// (data/ui/tl_import_strings.txt). No guest, OGRE or platform types.

#pragma once

#include <optional>

#include "mods/save_units.h"
#include "save_import/import_message.h"

namespace torchlight::mods {

// The box to show, or none when there is nothing to tell (no file changed, none left alone with
// unknown units, no failure, every unit the check counted on loaded).
std::optional<save_import::ImportMessage> SaveUnitsNotice(const SaveUnitsReport& report,
                                                          const save_import::Translate& tr);

}  // namespace torchlight::mods
