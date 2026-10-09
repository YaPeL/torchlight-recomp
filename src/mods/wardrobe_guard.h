// When to keep the game from building a character's wardrobe (src/mods/wardrobe_hooks.cpp,
// guest_abi/wardrobe.h): its first build reads the body model's entity without testing it, and a
// model the game could not make an entity for (an empty or missing mesh name, as a mod can leave
// one) makes that a read at a null pointer the game repeats forever. No guest types: the hook reads
// the fields and asks here.

#pragma once

#include <cstdint>

namespace torchlight::mods {

// The fields of a wardrobe the build branches on, as guest addresses (0: null).
struct WardrobeFields {
  uint32_t rebuild = 0;          // +68: set, the rebuild path, which does not use the model
  uint32_t wardrobe_entity = 0;  // +384: set, already built, nothing to do
  uint32_t node = 0;             // +16: null, nothing to build
  uint32_t model = 0;            // +12: the body model
  uint32_t model_entity = 0;     // the model's +92 (0 when there is no model)
};

// Whether the build would take its first-build path, the one that reads the model's entity.
bool WardrobeFirstBuild(const WardrobeFields& w);

// Whether to skip the build: the first-build path with no model or no entity. Skipping returns as
// the game does with no node; a later rebuild makes the wardrobe entity without the model.
bool SkipWardrobeBuild(const WardrobeFields& w);

}  // namespace torchlight::mods
