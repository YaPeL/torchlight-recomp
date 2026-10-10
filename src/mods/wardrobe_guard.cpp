#include "mods/wardrobe_guard.h"

namespace torchlight::mods {

bool WardrobeFirstBuild(const WardrobeFields& w) {
  return w.rebuild == 0 && w.wardrobe_entity == 0 && w.node != 0;
}

bool SkipWardrobeBuild(const WardrobeFields& w) {
  return WardrobeFirstBuild(w) && (w.model == 0 || w.model_entity == 0);
}

}  // namespace torchlight::mods
