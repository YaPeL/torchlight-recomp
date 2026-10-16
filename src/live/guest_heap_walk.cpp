// GuestPhysicalHeapUse (guest_heap_use.h): the SDK's heap API, apart from the summary so the
// summary's tests need no runtime.

#include "live/guest_heap_use.h"

#include <string>

#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

namespace torchlight::live {

HeapUse GuestPhysicalHeapUse(rex::memory::Memory* memory) {
  if (!memory) return {};
  rex::memory::BaseHeap* heap = memory->GetPhysicalHeap();
  if (!heap) return {};
  std::vector<HeapRegion> regions;
  const uint64_t end = uint64_t(heap->heap_base()) + heap->heap_size();
  for (uint64_t address = heap->heap_base(); address < end;) {
    rex::memory::HeapAllocationInfo info{};
    if (!heap->QueryRegionInfo(uint32_t(address), &info) || info.region_size == 0) break;
    regions.push_back({info.region_size, info.state == 0});
    address += info.region_size;
  }
  return SummarizeRegions(regions);
}

void LogGuestHeapAtLevelLoad(std::optional<int32_t> depth) {
  const HeapUse use = GuestPhysicalHeapUse(REX_KERNEL_MEMORY());
  constexpr double kMiB = 1024.0 * 1024.0;
  REXLOG_INFO("memory: level loaded (depth {}): guest physical heap {:.1f} MiB in use, {:.1f} MiB free, "
              "largest free block {:.1f} MiB",
              depth ? std::to_string(*depth) : std::string("?"), double(use.used) / kMiB, double(use.free) / kMiB,
              double(use.largest_free) / kMiB);
}

}  // namespace torchlight::live
