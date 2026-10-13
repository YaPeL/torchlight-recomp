// The classes the character creation screen offers with mods (docs/mods.md, section 7l). PC lists
// its classes as the buttons of media/UI/charactercreate.layout whose onClick is guiSelect1, each
// named after its class's player unit; mods that add classes ship their own copy of that layout. The
// list is the game's three classes first, then every other such button of the enabled mods'
// layouts, in the mods' search order and the layout's order, kept only when a player unit of that
// name exists. No guest, OGRE or platform types.

#pragma once

#include <filesystem>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "mods/mod_list.h"

namespace torchlight::mods {

// The Xbox screen's three classes, in its order (its buttons and pad grid, game_ui.h new_game_menu).
inline constexpr std::string_view kGameClasses[] = {"Destroyer", "Alchemist", "Vanquisher"};

// The names of a CEGUI layout's windows whose "onClick" property is guiSelect1 (any case), in the
// layout's order.
std::vector<std::string> ClassButtons(std::string_view layout_xml);

// The game's classes, then the other buttons in order, without repeats (names compared in any
// case) and without those whose name is not a player unit's (`player_units`: NAMEs, upper case).
std::vector<std::string> ClassList(const std::vector<std::vector<std::string>>& buttons_by_mod,
                                   const std::set<std::string>& player_units);

// The player units' NAMEs (upper case) the enabled mods of `plan` define (*.dat under
// media/units/players/, text definitions).
std::set<std::string> ModPlayerUnits(const std::filesystem::path& mods_folder, const ModPlan& plan);

// Each enabled mod's class buttons, in the plan's search order (media/UI/charactercreate.layout,
// any case; a mod without one gives none).
std::vector<std::vector<std::string>> ModClassButtons(const std::filesystem::path& mods_folder,
                                                      const ModPlan& plan);

}  // namespace torchlight::mods
