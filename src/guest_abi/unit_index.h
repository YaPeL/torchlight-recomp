// The game's unit index (docs/mods.md, section 7e): the table of every unit definition (GUID, name,
// file, type, levels, rarity) the game spawns, drops and resolves saves from. The Xbox build loads
// it ready-made from MEDIA/UNITDATA.RAW; PC Torchlight has no such file and builds the table in
// memory at every start, so a mod's new units are in it there and not on Xbox. The host extends
// the table for the player's mods with the game's own unit loading and property reads
// (src/mods/unit_index_install.cpp).

#pragma once

#include <cstdint>

#include "guest_abi/guest_functions.h"
#include "guest_abi/ogre_layout.h"

namespace torchlight::guest_abi::unit_index {

using functions::GuestFunction;

// [confirmed] Loads the index: r3 = the index object, r4 = const std::wstring* path. Reads the
// whole file through the data manager (sub_82398790 -> sub_82398968 -> sub_8239D5F8, mod-aware),
// then 4 groups (the loop counter 4 @0x82329700), each a u32 count and that many entries, every
// number byte-swapped from little-endian (@0x8232973C, @0x823297CC, lwbrx @0x82329A54). An entry
// whose GUID is already in the table replaces it (@0x82329AA8..@0x82329B0C) but not under the
// names the old one was filed by, so the file must be loaded once, complete. Its only caller is
// sub_82329310 (@0x82329408), with "MEDIA/UNITDATA.RAW" (@0x823293F8), called by the global data
// loader sub_8231FF28 @0x823204DC: after the data manager is built (@0x823200D0) and the mods are
// registered with it.
inline constexpr GuestFunction kLoadIndex{0x823296D0, Confidence::kConfirmed};

// [confirmed] Loads one unit definition and its BASEFILE chain: r3 = const std::wstring* path,
// r4 = the node list. Allocates a 56-byte node (@0x82329C44), fills it through the data manager
// (sub_82392C18 with r5 = 1 @0x82329C74: sub_8239E7D8 on the data manager 0x83559514 when it
// exists), appends it (sub_82363BC8 @0x82329C8C) and recurses on its BASEFILE (@0x82329C98,
// @0x82329D24): the list ends up child first, then each base.
inline constexpr GuestFunction kLoadUnitDefinition{0x82329C28, Confidence::kConfirmed};

// [confirmed] Property reads over a node list, as the index builder sub_82329E30 uses them. Each
// walks the list from the child and keeps going while the value found equals the default
// (@0x8232C4F8..@0x8232C508, @0x82329DA4..@0x82329E20): a unit that sets a value equal to the
// default inherits its base's (the Xbox file shows it, e.g. LEVEL 1 over a base's 0 gives 0).
//  - kReadU32: r3 = const std::wstring* name, r4 = const u32* default, r5 = list; returns the
//    value (unsigned compare @0x8232C504).
//  - kReadS32: the same, signed compare (@0x8232C58C).
//  - kReadString: r3 = std::wstring* result, r4 = name, r5 = const std::wstring* default,
//    r6 = list.
inline constexpr GuestFunction kReadU32{0x8232C490, Confidence::kConfirmed};
inline constexpr GuestFunction kReadS32{0x8232C518, Confidence::kConfirmed};
inline constexpr GuestFunction kReadString{0x82329D38, Confidence::kConfirmed};

namespace node_list {
// [confirmed] The builder's list on its stack (@0x8232A170..@0x8232A17C): data, count, capacity,
// growth (5); read by the property reads at +0, +4, +8 (@0x8232C4B8..@0x8232C4CC).
inline constexpr Field kData{0, Confidence::kConfirmed};
inline constexpr Field kCount{4, Confidence::kConfirmed};
inline constexpr Field kCapacity{8, Confidence::kConfirmed};
inline constexpr Field kGrowth{12, Confidence::kConfirmed};
inline constexpr uint32_t kGrowthValue = 5;
inline constexpr Size kSize{16, Confidence::kConfirmed};
}  // namespace node_list

}  // namespace torchlight::guest_abi::unit_index
