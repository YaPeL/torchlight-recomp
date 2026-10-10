// Publishing the host-built mod manager writes the data manager's +20 and the singleton, a high
// guest address, so it runs on a sparse 4 GiB arena, as the achievement hook tests do (Linux only:
// src/guest_abi/CMakeLists.txt).
#include "guest_abi/mods.h"

#include <sys/mman.h>

#include <cstdio>
#include <cstdlib>

namespace mods = torchlight::guest_abi::mods;
using torchlight::guest_abi::ReadU32;

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
  if (!ok) {
    ++failures;
    std::fprintf(stderr, "FAIL: %s\n", what);
  }
}
}  // namespace

int main() {
  {
    auto* arena = static_cast<uint8_t*>(
        mmap(nullptr, size_t{1} << 32, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0));
    Check(arena != MAP_FAILED, "arena");
    if (arena != MAP_FAILED) {
      const uint32_t data_manager = 0x40000000, built = 0x40100000;
      Check(ReadU32(arena, data_manager + 20) == 0 && ReadU32(arena, mods::kModManagerGlobal) == 0,
            "before: no mod manager anywhere, as in the original game");
      mods::PublishModManager(arena, data_manager, built);
      Check(ReadU32(arena, data_manager + 20) == built, "data manager +20 points at it");
      Check(ReadU32(arena, mods::kModManagerGlobal) == built, "the singleton points at it");
      munmap(arena, size_t{1} << 32);
    }
  }

  if (failures) return EXIT_FAILURE;
  std::puts("guest_abi mods publish test passed");
  return EXIT_SUCCESS;
}
