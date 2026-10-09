// A character's wardrobe (CWardrobe) and its body model (CGenericModel), for the guard against a
// model the game could not make an entity for (src/mods/wardrobe_hooks.cpp, docs/mods.md section
// 7f), with their evidence.

#pragma once

#include <cstdint>

#include "guest_abi/guest_functions.h"
#include "guest_abi/ogre_layout.h"

namespace torchlight::guest_abi::wardrobe {

using functions::GuestFunction;

// [confirmed] Builds a wardrobe's entities: r3 = the CWardrobe (vtable 0x820C2688, MSVC RTTI),
// r4 = a flag. Twelve callers; sub_822DB728 (@0x822DBCF4) ignores the result. Its first branches:
//  - kRebuild (+68) set (bne @0x822DC6E0): the rebuild path (0x822DC900), which makes the
//    wardrobe entity from +372 (0x822DEA90..0x822DEB60), not from the model;
//  - else kWardrobeEntity (+384) set (bne @0x822DC6EC): nothing to do, return (0x822DEF54);
//  - else kNode (+16) zero (beq @0x822DC6F8): nothing to do, return (0x822DC7C4 tests it again,
//    beq @0x822DC7CC);
//  - else the first build: "paperdoll_N" from *(*(kModel)+kEntity)+180 (@0x822DC72C, @0x822DC740,
//    @0x822DC744, no test of either pointer: the null read at 0xB4 when the model has no entity),
//    stored at kPaperdollEntity (@0x822DC758), then "wardrobeentity_N" through the scene manager
//    at +392 (vtable +292, @0x822DC7A4), stored at kWardrobeEntity (@0x822DC7A8).
inline constexpr GuestFunction kBuild{0x822DC6B0, Confidence::kConfirmed};

namespace layout {
// [confirmed] The body model, a CGenericModel* (@0x822DC72C; vtable 0x820035A4 in a fault dump).
inline constexpr Field kModel{12, Confidence::kConfirmed};
// [confirmed] Tested before any build (@0x822DC6F0, 0x822DC7C4, 0x822DEA90): no node, nothing is
// built.
inline constexpr Field kNode{16, Confidence::kConfirmed};
// [confirmed] Set: the rebuild path (@0x822DC6C4, bne @0x822DC6E0).
inline constexpr Field kRebuild{68, Confidence::kConfirmed};
// [confirmed] The paperdoll entity (stw @0x822DC758); the wardrobe's release (sub_822DC0F8) tests
// it before use.
inline constexpr Field kPaperdollEntity{376, Confidence::kConfirmed};
// [confirmed] The wardrobe entity (stw @0x822DC7A8); tested before the first build (@0x822DC6E4)
// and by the release (sub_822DC0F8). The other reads at +376/+384 near it (sub_822D9778, slot 61 of
// CTriggerUnit's vtable 0x82004894) are a unit's fields, not the wardrobe's.
inline constexpr Field kWardrobeEntity{384, Confidence::kConfirmed};
}  // namespace layout

namespace model {
// [confirmed] CGenericModel's load, sub_822BFF48: r3 = the model, r4 = the mesh name. It creates
// the entity (sub_8238FB60 @0x822C0088) and stores it at kEntity (@0x822C0090); without one it logs
// "Unable to find file : " and "[Genericmodel] Error creating entity" (0x820C18FC @0x822C00D8,
// 0x820C1914 @0x822C0118) to the game's own log and stores 0 (@0x822C0170). The title screen's
// Destroyer got the mesh "/.mesh" with JCC - Main (docs/mods.md, 7f), and a null entity.
inline constexpr Field kEntity{92, Confidence::kConfirmed};
// [confirmed] The mesh name, a std::wstring the load compares and assigns (r29 = model + 248
// @0x822BFFD0, its capacity at +268 read @0x822BFFC8 and tested against 8).
inline constexpr Field kMeshName{248, Confidence::kConfirmed};
}  // namespace model

// [confirmed] Makes a unit by name: r3 = owner, r4 = the name (a UTF-16 string), r5 = a flag. Looks
// the name up in the unit index (sub_8232B068 on the singleton 0x835594EC, @0x823DE924), allocates
// the unit (2576 bytes, @0x823DE940) and loads it through its vtable +244 (@0x823DE974,
// @0x823DE97C); the title screen's Destroyer is made here, and its wardrobe built inside that call.
inline constexpr GuestFunction kMakeUnit{0x823DE8F0, Confidence::kConfirmed};

}  // namespace torchlight::guest_abi::wardrobe
