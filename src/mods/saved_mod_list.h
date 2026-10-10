// The mod list of a character save, repaired as it is written (docs/mods.md, section 7d). The Xbox
// build's unit save writer writes each mod name's u16 length as 0 (guest_abi/mods.h
// kUnitSaveWriter: the high half of a big-endian u32) and the reader then takes every name as
// empty, so a save made with mods never finishes loading. Right after the writer returns, the host
// finds the list in the bytes that call wrote and puts the lengths in: big-endian u16, as the
// reader reads them and as PC and the save converter write them. The save's size does not change.
// No guest, OGRE or platform types.

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace torchlight::mods {

// The list as the reader expects it: the u32 written before it (`before`), the count, then per
// name its length and its UTF-16 units, all big-endian. With `runic_bug`, every length is 0, as
// the Xbox writer leaves it.
std::vector<uint8_t> EncodeSavedModList(uint32_t before, const std::vector<std::u16string>& names,
                                        bool runic_bug);

enum class ModListRepair {
  kNothing,    // no names: nothing to repair
  kRepaired,   // found exactly once, lengths written
  kNotFound,   // the written bytes do not hold the list as the bug leaves it
  kAmbiguous,  // found more than once: left alone
  kTooLong,    // a name longer than a u16 length can state: left alone
};

// Finds, in `written` (exactly the bytes one call of the writer appended, from where the stream
// was when it started), the list `names` preceded by `before`, as the Xbox writer leaves it
// (EncodeSavedModList with runic_bug), and when it occurs exactly once writes each name's length.
// Any other outcome leaves `written` unchanged.
ModListRepair RepairSavedModList(std::span<uint8_t> written, uint32_t before,
                                 const std::vector<std::u16string>& names);

const char* ToString(ModListRepair result);

}  // namespace torchlight::mods
