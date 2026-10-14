// The wardrobe guard (wardrobe_guard.h): a build is skipped only on the first-build path, the one
// that reads the body model's entity, and only when there is no model or no entity; every other
// path goes to the game as it is.
#include <cstdio>

#include "mods/wardrobe_guard.h"

using namespace torchlight::mods;

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
  if (!ok) {
    ++failures;
    std::fprintf(stderr, "FAIL: %s\n", what);
  }
}

WardrobeFields FirstBuild() {
  WardrobeFields w;
  w.node = 0x40001000;
  w.model = 0x40002000;
  w.model_entity = 0x40003000;
  return w;
}
}  // namespace

int main() {
  WardrobeFields w = FirstBuild();
  Check(WardrobeFirstBuild(w), "node, no rebuild, no wardrobe entity: the first build");
  Check(!SkipWardrobeBuild(w), "a model with an entity is built");

  w.model_entity = 0;
  Check(SkipWardrobeBuild(w), "a model without an entity is skipped (the title screen's Destroyer)");
  w.model = 0;
  Check(SkipWardrobeBuild(w), "no model is skipped");

  w = FirstBuild();
  w.model_entity = 0;
  w.rebuild = 0x40004000;
  Check(!WardrobeFirstBuild(w) && !SkipWardrobeBuild(w), "the rebuild path does not read the model");

  w = FirstBuild();
  w.model_entity = 0;
  w.wardrobe_entity = 0x40005000;
  Check(!SkipWardrobeBuild(w), "an already built wardrobe returns in the game");

  w = FirstBuild();
  w.model_entity = 0;
  w.node = 0;
  Check(!WardrobeFirstBuild(w) && !SkipWardrobeBuild(w), "no node returns in the game");

  if (failures) return 1;
  std::puts("wardrobe guard: ok");
  return 0;
}
