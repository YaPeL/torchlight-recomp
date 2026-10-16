// The guest's physical memory at a level load, for the log (one line per load): what is in use,
// what is free and the largest free block. The game reserves large contiguous blocks there (a
// 16 MiB reservation failed on a level change with a large texture pack), so the largest free
// block shows fragmentation that the free total hides. SummarizeRegions is apart from the SDK so
// it can be tested.

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace rex::memory {
class Memory;
}

namespace torchlight::live {

struct HeapRegion {
  uint32_t size = 0;
  bool free = false;
};

struct HeapUse {
  uint64_t used = 0;          // reserved or committed, bytes
  uint64_t free = 0;          // bytes
  uint64_t largest_free = 0;  // the largest run of free pages, bytes
};

// Adjacent free regions count as one block.
HeapUse SummarizeRegions(const std::vector<HeapRegion>& regions);

// The SDK's physical heap (the pages behind the guest's physical views), walked region by region.
// None when there is no memory.
HeapUse GuestPhysicalHeapUse(rex::memory::Memory* memory);

// The game's thread, after a level load: the line for the log ("memory: level loaded (depth D):
// ..."), with the loaded level's depth when known.
void LogGuestHeapAtLevelLoad(std::optional<int32_t> depth);

}  // namespace torchlight::live
