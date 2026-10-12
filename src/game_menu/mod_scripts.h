// Which material scripts are a mod's, for the game's script listener (mods_install.cpp,
// HookScriptListener), without the guest: a mod's material that repeats a name already defined is
// ignored, as on PC (docs/mods.md 7j).

#pragma once

#include <set>
#include <string>
#include <string_view>

namespace torchlight::game_menu {

class ModScriptGroups {
 public:
  // An active mod's resource group (RegisterMods).
  void Add(std::string group);
  // Whether a "createMaterial" event comes from a mod's script: told by the group the script is
  // compiled for (the one being initialised). The event's file name is relative to its resource
  // location, so it shows no device and is not what decides.
  bool FromMod(std::string_view file, std::string_view group) const;

 private:
  std::set<std::string, std::less<>> groups_;
};

}  // namespace torchlight::game_menu
