#include "game_menu/mod_scripts.h"

#include <utility>

namespace torchlight::game_menu {

void ModScriptGroups::Add(std::string group) { groups_.insert(std::move(group)); }

bool ModScriptGroups::FromMod(std::string_view /*file*/, std::string_view group) const {
  return groups_.contains(group);
}

}  // namespace torchlight::game_menu
