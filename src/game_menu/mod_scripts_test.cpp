// Tests for telling a mod's material script (mod_scripts.h).

#include <cstdio>
#include <cstdlib>

#include "game_menu/mod_scripts.h"

namespace {

using torchlight::game_menu::ModScriptGroups;

void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    std::exit(1);
  }
}

void TestByGroup() {
  ModScriptGroups groups;
  groups.Add("TNNR");
  groups.Add("Some Mod");
  // The game hands the file name relative to the mod's location: no device to tell it by.
  Check(groups.FromMod("MEDIA/MATERIALS/MINE.MATERIAL", "TNNR"), "relative file, mod's group");
  Check(groups.FromMod("mine.material", "Some Mod"), "bare file, mod's group");
  Check(!groups.FromMod("MEDIA/MATERIALS/MINE.MATERIAL", "General"), "pak's group");
  Check(!groups.FromMod("MEDIA/MATERIALS/MINE.MATERIAL", "Popular"), "another pak group");
  // The replacements are in the pak's groups and redefine, as files inside the pak would.
  Check(!groups.FromMod("tlrepl:/MEDIA/MATERIALS/MINE.MATERIAL", "General"), "replacements");
  Check(!groups.FromMod("MEDIA/MATERIALS/MINE.MATERIAL", "tnnr"), "group names are exact");
}

void TestNoMods() {
  const ModScriptGroups groups;
  Check(!groups.FromMod("MEDIA/MATERIALS/MINE.MATERIAL", "General"), "no mods");
  Check(!groups.FromMod("", ""), "empty event");
}

}  // namespace

int main() {
  TestByGroup();
  TestNoMods();
  std::puts("mod_scripts_test: ok");
  return 0;
}
