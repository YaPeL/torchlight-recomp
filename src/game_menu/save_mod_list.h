// The unit save writer with its mod-list bug repaired (docs/mods.md, section 7d): what the hook on
// guest_abi/mods.h kUnitSaveWriter does around the original, on guest memory, with the original
// passed in so a test can stand in for it. No rex or PPC types.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include "mods/saved_mod_list.h"

namespace torchlight::game_menu {

struct UnitSaveOutcome {
  enum class Kind {
    kNoNames,             // the unit has no mod names: the original ran once, untouched
    kRepaired,            // the original ran once; the lengths were written into its bytes
    kWrittenWithoutList,  // fallback: rewound and written again with the list emptied
  };
  Kind kind = Kind::kNoNames;
  mods::ModListRepair repair = mods::ModListRepair::kNothing;  // why, for the fallback
  size_t names = 0;
};

// Writes the unit `save_data` into the in-memory save `stream` with `write_unit` (the original
// writer, which reads both from guest memory), then repairs the mod list in the bytes it wrote. If
// the list cannot be repaired, rewinds the stream (position +16 and size +24) to where it was and
// calls `write_unit` again with the unit's name count set to 0, restoring the count afterwards.
// Calling the original twice is safe: it only writes the stream (and the guest's write-error flag,
// only on a failed write); the unit and its items are only read (guest_abi/mods.h).
UnitSaveOutcome WriteUnitSave(uint8_t* base, uint32_t save_data, uint32_t stream,
                              const std::function<void()>& write_unit);

}  // namespace torchlight::game_menu
