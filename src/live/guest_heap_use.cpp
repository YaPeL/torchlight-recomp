#include "live/guest_heap_use.h"

#include <algorithm>

namespace torchlight::live {

HeapUse SummarizeRegions(const std::vector<HeapRegion>& regions) {
  HeapUse use;
  uint64_t run = 0;
  for (const HeapRegion& r : regions) {
    if (r.free) {
      use.free += r.size;
      run += r.size;
      use.largest_free = std::max(use.largest_free, run);
    } else {
      use.used += r.size;
      run = 0;
    }
  }
  return use;
}

}  // namespace torchlight::live
