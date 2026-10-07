// The host-built mod manager's initial layout and the active-mod count, on synthetic memory.
#include "guest_abi/mods.h"

#include <cstdio>
#include <cstdlib>
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

  if (failures) return EXIT_FAILURE;
  std::puts("guest_abi mods tests passed");
  return EXIT_SUCCESS;
}
