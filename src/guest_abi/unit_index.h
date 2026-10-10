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

namespace index {
// [confirmed] The index object, r3 of kLoadIndex. sub_82329310 builds it, makes it the singleton
// (@0x823293EC, only when that is null) and only then loads it. Its entries by GUID: a std::map at
// +60, initialised by sub_82258648 (@0x823293A8: +64 the head node, +68 the size, zeroed), looked
// up by the loader (sub_82259E50 @0x82329AA8, the result compared with the head @0x82329AAC) and
// set through operator[] (sub_82327718 @0x82329AC4). A GUID already there replaces the entry; an
// entry whose NAME (the first string, read @0x823297C0 into r1+192) is empty is destroyed without
// being filed (its length @0x82329B10, beq @0x82329B18). So +68 counts the distinct GUIDs that
// have an entry with a name: 3376 of the Xbox file's 3489, 113 entries having none. The loader reads the whole file through a reader
// (sub_82398790 @0x823296E4); when that holds nothing (the file was not found) it skips every
// group (@0x823296F0) and leaves the map empty, with no error.
inline constexpr uint32_t kSingleton = 0x835594EC;
inline constexpr Field kGuidMapSize{68, Confidence::kConfirmed};
}  // namespace index

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

// [confirmed] Empties a node list: r3 = list. Destroys each node through slot 0 of its vtable
// (r4 = 1, @0x823D403C..@0x823D404C), frees the buffer (@0x823D4088) and zeroes data, count and
// capacity; the game's builder calls it after each definition (@0x8232AC20). Not sub_82392B50,
// which the builder calls just before: that one writes the definition back to a file (the build
// tool's compile step). Our builder does not call it (names, below; docs/mods.md 7g).
inline constexpr GuestFunction kClearNodeList{0x823D3FF8, Confidence::kConfirmed};

namespace names {
// [confirmed] Property and group names are numbers into one global table, a std::map at +4 from a
// key (node +12) to the name, a std::wstring (node +16) (sub_821A1608 @0x821A1640, 0x821A164C);
// a property keeps its key at +0x10 and the table at +0x14 (sub_821A1510 @0x821A1570,
// @0x821A155C), and a lookup by name compares every property's name (sub_821A1510): a property
// whose key is no longer in the table is not found, and the read returns its default. The same
// address in every run (gdb, 2026-10-09), also the default table a group's destructor compares
// with (@0x82391AA4).
inline constexpr uint32_t kTable = 0x8355C2D0;
// [confirmed] The map's head node (compared with the lookup's result @0x821A1648) and size.
inline constexpr Field kTableHead{8, Confidence::kConfirmed};
inline constexpr Field kTableSize{12, Confidence::kConfirmed};
// [confirmed] Releases a name: r3 = table, r4 = key; gets the name (@0x823932C4) and, unless it is
// the table's default (+0x50, @0x82393314), hands it to sub_823933E8 (@0x8239333C), which takes
// the key out of the table once nothing uses it. Called by CDataGroup's destructor for the group's
// own name (@0x8239198C) and by the properties' destructors (sub_82393B80, sub_82393C28,
// sub_82393D00, sub_82393E10) and sub_82392080. The destructor (sub_82391950) releases nothing
// and leaves the properties alone for a node that shares another group's (+0x35 set, bne
// @0x82391978), which kLoadUnitDefinition makes when the unit index already has the file
// (sub_82392C18: sub_8239E7D8 looks it up in the index, singleton 0x835594EC, @0x82392C58, and the
// node is marked shared @0x82392C6C). While our builder runs the index is still empty, so every
// definition is read from its file into nodes of their own (@0x82392E38, sub_8239D5F8), and
// emptying the list released their names. Which object went on using a released key was not
// traced; keeping the nodes keeps every name the build made. Measured (gdb, 2026-10-09,
// JCC - Main): during the build the table kept ~17,300 names while its largest key went from
// 17,830 to 90,576; the title screen's Destroyer then had 44 of its 72 properties with keys no
// longer in the table (40583..41050, RESOURCEDIRECTORY and MESHFILE among them), so it got an
// empty mesh. With the index from the cache (no build) and without mods, 0.
inline constexpr GuestFunction kReleaseName{0x823932A8, Confidence::kConfirmed};
}  // namespace names

namespace group {
// [confirmed] A CDataGroup (vtable 0x820D2808): its properties, data +16 and count +20 (sub_821A1510
// @0x821A1538, @0x821A151C), and its subgroups, data +32 and count +36 (its destructor
// sub_82391950 @0x823919A0, @0x82391990).
inline constexpr Field kPropertyData{16, Confidence::kConfirmed};
inline constexpr Field kPropertyCount{20, Confidence::kConfirmed};
inline constexpr Field kSubgroupData{32, Confidence::kConfirmed};
inline constexpr Field kSubgroupCount{36, Confidence::kConfirmed};
}  // namespace group

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
