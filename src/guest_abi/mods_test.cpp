// The host-built mod manager's initial layout and the active-mod count, on synthetic memory.
#include "guest_abi/mods.h"

#include <sys/mman.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace mods = torchlight::guest_abi::mods;
using torchlight::guest_abi::ReadU16;
using torchlight::guest_abi::ReadU32;
using torchlight::guest_abi::WriteU32;

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
  std::vector<uint8_t> memory(0x40000, 0xCD);  // garbage everywhere first
  uint8_t* base = memory.data();
  const uint32_t manager = 0x10000;

  mods::InitModManager(base, manager);
  Check(ReadU32(base, manager) == mods::kRunicCoreVtable, "vtable: CRunicCore's own");
  Check(ReadU32(base, manager + 4) == 0, "+4 = 0, so the base destructor does nothing");
  Check(ReadU16(base, manager + 8) == 0, "folder: empty, terminated inline");
  Check(ReadU32(base, manager + 8 + 0x10) == 0, "folder: length 0");
  Check(ReadU32(base, manager + 8 + 0x14) == 7, "folder: inline capacity 7 (wchar_t)");
  Check(ReadU32(base, manager + 36) == 0 && ReadU32(base, manager + 40) == 0 &&
            ReadU32(base, manager + 44) == 0,
        "empty list");
  Check(ReadU32(base, manager + 48) == 1, "list growth word 1, as PC");
  Check(base[manager + 52] == 1 && base[manager + 53] == 0, "PC's flags");
  Check(base[manager + 54] == 0 && base[manager + 55] == 0, "the rest of the 0x38 bytes zeroed");
  Check(base[manager + 56] == 0xCD, "nothing written past the object");
  Check(mods::ActiveModCount(base, manager) == 0, "no mods: count 0");
  Check(mods::ActiveModCount(base, 0) == 0, "no manager: count 0");

  // Three mods: active, disabled by a negative priority, inactive flag.
  const uint32_t list = 0x11000, m1 = 0x12000, m2 = 0x13000, m3 = 0x14000;
  for (uint32_t m : {m1, m2, m3}) {
    for (uint32_t i = 0; i < mods::mod::kSize.bytes; ++i) base[m + i] = 0;
  }
  base[m1 + 192] = 1; WriteU32(base, m1 + 196, 1);
  base[m2 + 192] = 1; WriteU32(base, m2 + 196, static_cast<uint32_t>(-1));
  base[m3 + 192] = 0; WriteU32(base, m3 + 196, 3);
  WriteU32(base, list, m1); WriteU32(base, list + 4, m2); WriteU32(base, list + 8, m3);
  WriteU32(base, manager + 36, list); WriteU32(base, manager + 40, 3); WriteU32(base, manager + 44, 4);
  Check(mods::ActiveModCount(base, manager) == 1, "only active mods with priority >= 0 count");
  base[m3 + 192] = 1;
  Check(mods::ActiveModCount(base, manager) == 2, "priority 3, active: counts");

  // Publishing writes the data manager's +20 and the singleton (a sparse 4 GiB arena, as the
  // achievement hook tests use, since the singleton is a high guest address).
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
  // A unit's saved mod names: one inline (capacity 7), one behind a pointer, big-endian units.
  {
    const uint32_t save_data = 0x20000, names = 0x21000, heap = 0x22000;
    auto put_wstring = [&](uint32_t str, const std::u16string& text, uint32_t capacity, uint32_t at) {
      for (size_t c = 0; c < text.size(); ++c) {
        base[at + 2 * c] = static_cast<uint8_t>(text[c] >> 8);
        base[at + 2 * c + 1] = static_cast<uint8_t>(text[c]);
      }
      if (at != str) WriteU32(base, str, at);
      WriteU32(base, str + 16, static_cast<uint32_t>(text.size()));
      WriteU32(base, str + 20, capacity);
    };
    put_wstring(names, u"short", 7, names);
    put_wstring(names + 28, u"a longer mod name", 31, heap);
    WriteU32(base, save_data + 576, names);
    WriteU32(base, save_data + 580, 2);
    const auto read = mods::ReadSavedModNames(base, save_data);
    Check(read.size() == 2 && read[0] == u"short" && read[1] == u"a longer mod name",
          "saved mod names: inline and heap");
    WriteU32(base, save_data + 580, 0);
    Check(mods::ReadSavedModNames(base, save_data).empty(), "saved mod names: none");
    WriteU32(base, save_data + 580, 5000);
    Check(mods::ReadSavedModNames(base, save_data).empty(), "saved mod names: implausible count");
  }

  if (failures) return EXIT_FAILURE;
  std::puts("guest_abi mods tests passed");
  return EXIT_SUCCESS;
}
