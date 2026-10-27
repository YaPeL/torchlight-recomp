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

// The texture build's blend of an item's image into the base (docs/wardrobe-texture.md, "The blend,
// exactly"), for the host version of the loop (hooks/wardrobe_blend.h). Offsets on the build's
// stack are from its r1.
namespace blend {
// [confirmed] The midasm hook (config/torchlight_hooks.toml): after `rotlwi r26,r10,0` @0x822DD21C,
// the rows known non-zero (beq @0x822DD218); true jumps to kLoopExit, past the loop. The two must
// match config/torchlight_hooks.toml's address and jump_address_on_true;
// tools/wardrobe_blend/check_generated.py reads them from here and checks the generated hook.
inline constexpr uint32_t kHookAddress = 0x822DD21C;
// [confirmed] The loop's exit (loc_822DD39C, clrlwi r11,r14,24): reached from both loop ends
// (bne @0x822DD390 falls through to 0x822DD394, bne @0x822DD398 to the exit) and from the tests for
// no image (beq @0x822DD1E8) and no rows (beq @0x822DD218).
inline constexpr uint32_t kLoopExit = 0x822DD39C;
// [confirmed] The return address of the loop's packColour (bl @0x822DD380).
inline constexpr uint32_t kPackReturn = 0x822DD384;

// [confirmed] Ogre::PixelBox (OgrePixelFormat.h: Box's six uint32 then data, format): getPixelBox
// (sub_82472280) stores the data at +24 (stw r11,24(r31) @0x824723DC) and the format at +28
// (stw r10,28(r31) @0x824723B8).
inline constexpr uint32_t kPixelBoxData = 24;
inline constexpr uint32_t kPixelBoxFormat = 28;

namespace stack {
// [confirmed] The frame's size (stwu r1,-2064(r1) @0x822DC6C0).
inline constexpr uint32_t kFrameSize = 2064;
// [confirmed] Rows and columns, read @0x822DD208 (lwz r10,136) and at each row @0x822DD220
// (lwz r11,140); stored @0x822DC9C0, @0x822DC9BC.
inline constexpr uint32_t kRows = 136;
inline constexpr uint32_t kColumns = 140;
// [confirmed] The base's pixel box: a pointer to the copy at r1+1552 (stw @0x822DCAD4; the path
// that skips the copy, beq @0x822DC994, leaves it unset, and the guest then loads whatever is
// there). The loop's r31 is its data (lwz r11,164(r1) @0x822DD1EC, lwz r31,24(r11) @0x822DD200), loaded just before
// the item's getPixelBox (bl @0x822DD204), which writes only its own box at r1+432; the host reads
// the same memory after it.
inline constexpr uint32_t kBasePixelBox = 164;
// [confirmed] The item image's pixel box (getPixelBox sub_82472280 into r1+432, bl @0x822DD204): data
// +24, read as r29 @0x822DD214; format +28, the image's +28 (lwz r10,28(r30) @0x824723A4,
// stw r10,28(r31) @0x824723B8), equal to the loop's r27 (lwz r27,28(r21) @0x822DD210).
inline constexpr uint32_t kItemData = 432 + kPixelBoxData;
inline constexpr uint32_t kItemFormat = 432 + kPixelBoxFormat;

// [confirmed] The loop's temporaries, each with the instruction that writes it last in an
// iteration. unpackColour's outputs for the item's pixel (r3..r6 @0x822DD238..0x822DD244):
inline constexpr uint32_t kItemR = 129, kItemG = 130, kItemB = 128, kItemA = 131;
// for the base's pixel (r3..r6 @0x822DD274..0x822DD280):
inline constexpr uint32_t kBaseR = 152, kBaseG = 145, kBaseB = 144, kBaseA = 288;
// the blended channels, over the item's (stb @0x822DD374..0x822DD37C), and the item's alpha slot
// set to r17 (stb r17,131 @0x822DD2EC);
// the integer-to-float doubles (std @0x822DD2A0..0x822DD2F8, the byte zero-extended; the alpha
// extsw'd):
inline constexpr uint32_t kItemGDouble = 384, kItemADouble = 336, kItemBDouble = 592, kItemRDouble = 304;
inline constexpr uint32_t kBaseGDouble = 600, kBaseRDouble = 296, kBaseBDouble = 528;
// the fctidz results (stfd f13/f11/f10,192 @0x822DD354..0x822DD36C; the last is red's), whose
// lowest byte (+199) is the channel.
inline constexpr uint32_t kTruncated = 192;
}  // namespace stack

// [confirmed] The only definitions that reach the loop (a reaching-definitions pass over the
// generated function, tools/wardrobe_blend/check_generated.py): f31 = 1/255 (lfs f31,92(r5)
// @0x822DCAF8) and f30 = 1.0 (lfs f30,64(r5) @0x822DCB00), r5 = 0x820AA2C0; r17 = 255
// (li r17,255 @0x822DCB44).
inline constexpr uint32_t kConstantPool = 0x820AA2C0;
inline constexpr uint32_t kAlphaScale = kConstantPool + 92;
inline constexpr uint32_t kOne = kConstantPool + 64;
inline constexpr uint8_t kBlendedItemAlpha = 255;
}  // namespace blend

}  // namespace torchlight::guest_abi::wardrobe
