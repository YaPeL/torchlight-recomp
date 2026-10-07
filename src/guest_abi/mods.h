// The guest's mod system (docs/mods.md): what the Xbox build keeps of PC's mod support, and the
// layout of the mod manager the host builds because the Xbox build has none.
//
// PC's mod manager is CModFileFilter (RTTI of vtable 0xA90E48, derived from CRunicCore), created
// by new(0x38) + constructor 0x5CE440 and stored at the data manager's +0xC (0x5C1AAA). The Xbox
// image has no RTTI, vtable or constructor for it; its non-virtual methods survive (below) because
// the data loader and the MODS check call them. The guest's data manager zeroes its pointer to it
// (+20, sub_8239B998) and nothing sets it, so every mod path is idle until the host builds one.

#pragma once

#include <cstdint>

#include "guest_abi/guest_functions.h"
#include "guest_abi/ogre_layout.h"

namespace torchlight::guest_abi::mods {

using functions::GuestFunction;

// ---------------------------------------------------------------------------------------------
// The data manager (the object that reads resources.cfg; game_ui.h kResourcesCfgLoader).

// [confirmed] Constructor sub_8239B998, called once: only by the global data loader sub_8231FF28
// (new(136) @0x823200BC, constructor @0x823200D0, stored at +92 @0x823200DC), itself called only
// by CGame's vtable slot 2 sub_82206000 (vtable 0x820ADFCC; loads plugins.cfg and sets the engine
// up: once per process).
// No data reload rebuilds it while the game runs.
inline constexpr GuestFunction kDataManagerCtor{0x8239B998, Confidence::kConfirmed};
// [confirmed] Its mod manager pointer: zeroed by the constructor (@0x8239B9D8), read by the data
// loader sub_8239D5F8 (@0x8239DC84, @0x8239DD8C), the global loader's MODS check (sub_8231FF28
// @0x82320A44, @0x82320A70, @0x82320A9C) and the destructor sub_8239CFB8, which calls slot 0 of
// its vtable (the deleting destructor, r4 = 1) when it is set and then zeroes it.
inline constexpr Field kDataManagerModManager{20, Confidence::kConfirmed};

// [confirmed] The mod manager singleton: read by sub_82256AA0 (@0x82257348), sub_8225F590
// (@0x8225F5DC), sub_82268180 (@0x822681D8) and sub_82268300 (@0x8226835C); never written in the
// image (tools/guest_re/xref.py const). PC's counterpart 0xEBBC7C is set by the constructor.
inline constexpr uint32_t kModManagerGlobal = 0x83559518;

// ---------------------------------------------------------------------------------------------
// The mod manager (PC CModFileFilter) as the guest's surviving methods use it.

namespace manager {
// [confirmed] PC size (new(0x38) @0x5C1A83).
inline constexpr Size kSize{0x38, Confidence::kConfirmed};
// [confirmed] CRunicCore base: vtable at +0, +4 = 0 (PC base constructor 0x5FC950). The guest's
// CRunicCore destructor sub_823DED38 only acts on +4 when it is not 0.
inline constexpr Field kVtable{0x00, Confidence::kConfirmed};
inline constexpr Field kBaseLink{0x04, Confidence::kConfirmed};
// [confirmed] The mods folder, a std::wstring (PC +0x08, assigned by 0x5CE4FE); no surviving
// guest method reads it. Initialised empty with the guest STL's layout (ogre_layout.h stl_string,
// wchar_t units: inline capacity 7, as kWStringFromText @0x821AC4A8..@0x821AC4C0).
inline constexpr Field kFolder{0x08, Confidence::kConfirmed};
inline constexpr uint32_t kWStringInlineCapacity = 7;
// [confirmed] The list of mods (CMod*): data +36, count +40, capacity +44 (PC +0x24..+0x2C, set
// to 0 by 0x5CE49C..0x5CE4A1); read by sub_823AA550, sub_823AAC48, sub_823AA988, sub_823AA630,
// sub_82256AA0, sub_8225F590 and sub_82268180, grown by sub_8230AB58 on the list at +36.
inline constexpr Field kListData{36, Confidence::kConfirmed};
inline constexpr Field kListCount{40, Confidence::kConfirmed};
inline constexpr Field kListCapacity{44, Confidence::kConfirmed};
// [confirmed] The list's fourth word, 1 (PC +0x30, 0x5CE4A4), read by the growth sub_8230AB58
// (+12 of the list).
inline constexpr Field kListGrowth{48, Confidence::kConfirmed};
inline constexpr uint32_t kListGrowthValue = 1;
// [confirmed] PC +0x34 = 1, +0x35 = 0 (0x5CE4AB, 0x5CE4AF): flags of PC's mods.dat handling
// (destructor 0x5CDBB0 tests +0x34); no surviving guest method reads them.
inline constexpr Field kFlagA{52, Confidence::kConfirmed};
inline constexpr Field kFlagB{53, Confidence::kConfirmed};
}  // namespace manager

// [confirmed] CRunicCore's own vtable (RTTI .?AVCRunicCore@@, COL 0x8211F770): slot 0 is its
// deleting destructor sub_823DECE0 (base destructor sub_823DED38, then sub_821CD9A8 when bit 0 of
// r4 is set). The host's manager uses it: CModFileFilter's vtable does not exist on Xbox, and the
// data manager's destructor only calls slot 0. The list's buffer and the CMod objects are not
// freed then (PC's destructor would); that is at shutdown only (kDataManagerCtor).
inline constexpr uint32_t kRunicCoreVtable = 0x820D61FC;
// [confirmed] CRunicCore's instance counter, incremented by every CRunicCore constructor
// (sub_8239B998 @0x8239BA00, sub_823A9EA0 @0x823A9EE8); PC 0xF36C48 (0x5FC96B).
inline constexpr uint32_t kRunicCoreInstances = 0x8355A2A0;

// [confirmed] The game's allocator (r3 = 0, r4 = size; returns the block) and its free, the pair
// sub_823AA550 uses for a CMod (@0x823AA5D4) and CRunicCore's deleting destructor frees with.
inline constexpr GuestFunction kAlloc{0x821CD7F8, Confidence::kConfirmed};
inline constexpr GuestFunction kFree{0x821CD9A8, Confidence::kConfirmed};

// ---------------------------------------------------------------------------------------------
// The surviving methods.

// [confirmed] Adds a mod: r3 = manager, r4 = const std::wstring* folder. Takes the highest
// priority in the list, allocates a CMod (200 bytes), builds it with sub_823A9EA0(mod, folder,
// highest + 1) and appends it. Its only caller sub_82268300 is unreferenced (dead).
inline constexpr GuestFunction kAddMod{0x823AA550, Confidence::kConfirmed};
// [confirmed] Active mods (CMod +192 set and +196 >= 0): r3 = manager.
inline constexpr GuestFunction kActiveModCount{0x823AAC48, Confidence::kConfirmed};

namespace mod {
// [confirmed] A CMod (vtable 0x820D30AC, RTTI .?AVCMod@@), as sub_823A9EA0 builds it.
inline constexpr Size kSize{200, Confidence::kConfirmed};
inline constexpr Field kFiles{36, Confidence::kConfirmed};     // file map, filled by sub_823AA340
inline constexpr Field kFolder{164, Confidence::kConfirmed};   // std::wstring, ends in '\'
inline constexpr Field kActive{192, Confidence::kConfirmed};   // u8, set to 1
inline constexpr Field kPriority{196, Confidence::kConfirmed}; // s32, < 0 = disabled
}  // namespace mod

// The mod manager's initial state, as PC's constructor leaves it, written at `at` (kSize bytes
// already allocated in guest memory). Does not register the instance (kRunicCoreInstances) or
// publish the pointer.
inline void InitModManager(uint8_t* base, uint32_t at) {
  for (uint32_t i = 0; i < manager::kSize.bytes; ++i) base[at + i] = 0;
  WriteU32(base, at + manager::kVtable.offset, kRunicCoreVtable);
  WriteU32(base, at + manager::kBaseLink.offset, 0);
  const uint32_t folder = at + manager::kFolder.offset;
  WriteU32(base, folder + ogre::stl_string::kLength.offset, 0);
  WriteU32(base, folder + ogre::stl_string::kCapacity.offset, manager::kWStringInlineCapacity);
  WriteU32(base, at + manager::kListGrowth.offset, manager::kListGrowthValue);
  base[at + manager::kFlagA.offset] = 1;
  base[at + manager::kFlagB.offset] = 0;
}

// What kActiveModCount returns, read from the host (for tests and diagnostics).
inline uint32_t ActiveModCount(const uint8_t* base, uint32_t manager_address) {
  if (!manager_address) return 0;
  const uint32_t data = ReadU32(base, manager_address + manager::kListData.offset);
  const uint32_t count = ReadU32(base, manager_address + manager::kListCount.offset);
  if (!data || count > 4096) return 0;
  uint32_t active = 0;
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t m = ReadU32(base, data + 4 * i);
    if (!m) continue;
    if (base[m + mod::kActive.offset] &&
        static_cast<int32_t>(ReadU32(base, m + mod::kPriority.offset)) >= 0) {
      ++active;
    }
  }
  return active;
}

}  // namespace torchlight::guest_abi::mods
