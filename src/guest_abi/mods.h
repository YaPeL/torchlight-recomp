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
#include <string>
#include <vector>

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
// [confirmed] Finds a file in the mods: r3 = std::wstring* result (the file's full path, empty when
// no mod has it), r4 = manager, r5 = const std::wstring* request, r6/r7 = outputs filled from the
// mod that has it (+52, +8). Walks the list in order, skipping mods with a negative priority
// (@0x823AA9F4) or inactive (@0x823AAA14), finds the request in each mod's file map (+36,
// sub_82328620 @0x823AAA38: exact comparison, sub_821A1478) and returns the first match's path
// (@0x823AAA50). The map's keys are the files' paths inside the mod (sub_823AA340 removes the mod
// folder's length @0x823AA448..@0x823AA454), upper case with '/' between folders (as a run's
// diagnostics showed: "MOD.DAT" -> "TLMODS:/TL_TEST_NEW_ITEM/MOD.DAT"), listed recursively with
// "*.*" (sub_823A0B30; subfolders through sub_823A1010 with "/*.*", 0x820D2F30): the runtime's
// wildcard must match names without a dot there, as Windows and the Xbox do (patches/README.md,
// rexglue-vfs-wildcard-dos-semantics.patch), or no subfolder of a mod is ever seen.
// Its only caller is the data manager's sub_8239D0E8 (@0x8239D164). PC indexes its list by
// PRIORITY (0x5CE85B..0x5CE8D2) and takes the first match too: the lowest PRIORITY wins.
inline constexpr GuestFunction kModFileLookup{0x823AA988, Confidence::kConfirmed};

namespace mod {
// [confirmed] A CMod (vtable 0x820D30AC, RTTI .?AVCMod@@), as sub_823A9EA0 builds it.
inline constexpr Size kSize{200, Confidence::kConfirmed};
inline constexpr Field kFiles{36, Confidence::kConfirmed};     // file map, filled by sub_823AA340
inline constexpr Field kFolder{164, Confidence::kConfirmed};   // std::wstring, ends in '\'
inline constexpr Field kActive{192, Confidence::kConfirmed};   // u8, set to 1
inline constexpr Field kPriority{196, Confidence::kConfirmed}; // s32, < 0 = disabled
}  // namespace mod

// ---------------------------------------------------------------------------------------------
// The mods a character save records (docs/mods.md, section 7d). Saving, sub_822D66A8 asks the data
// manager for the active mods' names (sub_8239E718 @0x822D67C8: data manager +20, each CMod with
// +192 set, its name at +80; none without a manager) and appends them to the unit's save data
// (+576, @0x822D67E0). Loading, sub_822D6858 copies them to the player (+2524, @0x822D69F8), and
// the character menu sub_8238C3A8 compares them with the active mods (@0x8238C818..@0x8238C930)
// and shows the CharacterModsWarning window when they differ (@0x8238C944; the window, found by
// name in sub_8238BEE8 @0x8238C094, is in the Xbox layouts too).

// [confirmed] The unit's save writer: r3 = the unit's save data, r4 = the save stream (@0x822A68FC,
// @0x822A6900). Called by the character save sub_8221CC70 (@0x8221D1D4, @0x8221D81C) and
// sub_822FBD90 (@0x822FBFC8). Its only callees write to the stream (sub_823A20E8, sub_823A24D8),
// recurse into the item writer sub_822C9AB8 (same two) or handle temporary strings: nothing reaches
// the file during the call. The character save then hashes the stream (sub_823A2610 @0x8221D9E8)
// and writes it out (sub_823A1F38 @0x8221D9F0).
//
// Runic difference (a bug of the Xbox build, latent because the list was always empty without a
// mod manager): the list is written as a u32 count (@0x822A7490) and, per name, the length loaded
// as a u32 (@0x822A74F4) and stored on the stack, of which 2 bytes are written (@0x822A74FC): on
// big-endian those are the high half, 0. The name's UTF-16 units follow with the right size
// (@0x822A7564). The reader sub_822A7D78 takes a u16 length (lhz @0x822A8A44) before each name
// (sub_823A1A98 @0x822A8A4C), so it reads every name as empty and the following bytes as the next
// fields: a save made with mods never finishes loading. PC (little-endian) writes the low half.
inline constexpr GuestFunction kUnitSaveWriter{0x822A68F0, Confidence::kConfirmed};

namespace unit_save {
// [confirmed] The u32 written right before the mod list (@0x822A7468, 4 bytes).
inline constexpr Field kBeforeModNames{568, Confidence::kConfirmed};
// [confirmed] The mod names: a vector of std::wstring, data +576, count +580 (@0x822A74D8,
// @0x822A7490; the reader appends to it @0x822A89C0).
inline constexpr Field kModNamesData{576, Confidence::kConfirmed};
inline constexpr Field kModNamesCount{580, Confidence::kConfirmed};
// [confirmed] One std::wstring per name (the writer steps 28 bytes); length +16 (@0x822A74F4), text
// inline while the capacity +20 is below 8 (@0x822A7544), otherwise behind the pointer at +0.
inline constexpr uint32_t kNameStride = 28;
}  // namespace unit_save

namespace save_stream {
// [confirmed] The in-memory save stream (sub_823A20E8): the buffer's start at +0 (@0x823A21C0,
// written at start + position @0x823A21CC), the write position at +16 (s64, @0x823A211C, advanced
// @0x823A21E0) and the size written so far at +24 (s64, raised to the position @0x823A21EC). The
// file gets +24 bytes from +0 (sub_823A1F38 @0x823A1FA0, @0x823A1FAC).
inline constexpr Field kBuffer{0, Confidence::kConfirmed};
inline constexpr Field kPosition{16, Confidence::kConfirmed};
inline constexpr Field kSize{24, Confidence::kConfirmed};
}  // namespace save_stream

// The mod names in a unit's save data, as UTF-16 code units (big-endian in guest memory). Empty
// on an implausible count or length (more than `max_names`, or more than 0xFFFF units).
inline std::vector<std::u16string> ReadSavedModNames(const uint8_t* base, uint32_t save_data,
                                                     uint32_t max_names = 1024) {
  std::vector<std::u16string> names;
  const uint32_t data = ReadU32(base, save_data + unit_save::kModNamesData.offset);
  const uint32_t count = ReadU32(base, save_data + unit_save::kModNamesCount.offset);
  if (!data || count > max_names) return names;
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t str = data + i * unit_save::kNameStride;
    const uint32_t length = ReadU32(base, str + ogre::stl_string::kLength.offset);
    const uint32_t capacity = ReadU32(base, str + ogre::stl_string::kCapacity.offset);
    if (length > 0xFFFF) return {};
    const uint32_t text = capacity > manager::kWStringInlineCapacity ? ReadU32(base, str) : str;
    std::u16string name(length, u'\0');
    for (uint32_t c = 0; c < length; ++c) name[c] = static_cast<char16_t>(ReadU16(base, text + 2 * c));
    names.push_back(std::move(name));
  }
  return names;
}

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

// Makes `manager_address` the game's mod manager: the data manager's +20 and the singleton, the two
// places the guest reads it from (the host builds it only when the player has mods; without, both
// stay 0 as in the original game).
inline void PublishModManager(uint8_t* base, uint32_t data_manager, uint32_t manager_address) {
  WriteU32(base, data_manager + kDataManagerModManager.offset, manager_address);
  WriteU32(base, kModManagerGlobal, manager_address);
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

// ---------------------------------------------------------------------------------------------
// The guest's directory search (XAPI FindFirstFileA/FindNextFileA, statically linked), with which
// the mods' file maps are listed (sub_823A1010 calls both). Read only by the mods diagnostics.

namespace find {
// [confirmed] FindFirstFileA(r3 = path, r4 = WIN32_FIND_DATAA*) -> handle, -1 on failure: calls
// kFindFirstNative with the path; on success (bge 0x8287E130) kFindDataFromNative into r4.
inline constexpr GuestFunction kFindFirstFile{0x8287E0C8, Confidence::kConfirmed};
// [confirmed] FindNextFileA(r3 = handle, r4 = WIN32_FIND_DATAA*) -> 1, or 0 when the search ends
// or fails: kFindNextNative, then on success (bge 0x8287E18C) kFindDataFromNative into r4.
inline constexpr GuestFunction kFindNextFile{0x8287E158, Confidence::kConfirmed};
// [confirmed] The native search under kFindFirstFile -> NTSTATUS. Splits the path at its last '\'
// (backwards scan, cmplwi 92): without one it returns 0xC000000D and opens nothing (0x82883BDC);
// a pattern of exactly "*.*" is made empty, i.e. match all (before 0x82883B40). Opens the folder
// with NtOpenFile (bl 0x83026DBC: access 0x00100001, share 3, options 0x4021) and lists it with
// NtQueryDirectoryFile through the import table (bctrl; FileName = the pattern, RestartScan 0).
inline constexpr GuestFunction kFindFirstNative{0x82883A68, Confidence::kConfirmed};
// [confirmed] The native search under kFindNextFile -> NTSTATUS (NtQueryDirectoryFile, bctrl).
inline constexpr GuestFunction kFindNextNative{0x82883BF0, Confidence::kConfirmed};
// [confirmed] Native entry -> WIN32_FIND_DATAA (sub_82883F88): attributes to +0 (from +56), the
// name to +44 (from +64, length +60), NUL-terminated.
inline constexpr GuestFunction kFindDataFromNative{0x82883F88, Confidence::kConfirmed};
namespace find_data {
inline constexpr Field kAttributes{0, Confidence::kConfirmed};
inline constexpr Field kFileName{44, Confidence::kConfirmed};
inline constexpr uint32_t kFileNameCapacity = 260;
inline constexpr uint32_t kAttributeDirectory = 0x10;  // FILE_ATTRIBUTE_DIRECTORY
}  // namespace find_data
}  // namespace find

}  // namespace torchlight::guest_abi::mods
