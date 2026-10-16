// Tests for the guest heap's summary (guest_heap_use.h): totals, and the largest free block with
// adjacent free regions joined.

#include <cstdio>
#include <cstdlib>

#include "live/guest_heap_use.h"

namespace {

using namespace torchlight::live;

void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    std::exit(1);
  }
}

}  // namespace

int main() {
  const HeapUse empty = SummarizeRegions({});
  Check(empty.used == 0 && empty.free == 0 && empty.largest_free == 0, "no regions");
  const HeapUse use = SummarizeRegions({{4096, false}, {8192, true}, {4096, true}, {4096, false}, {16384, true}});
  Check(use.used == 8192, "used: the regions in use");
  Check(use.free == 28672, "free: every free region");
  Check(use.largest_free == 16384, "the largest block: the last one");
  const HeapUse joined = SummarizeRegions({{4096, false}, {12288, true}, {12288, true}, {4096, false}});
  Check(joined.largest_free == 24576, "adjacent free regions are one block");
  std::puts("guest_heap_use_test: ok");
  return 0;
}
