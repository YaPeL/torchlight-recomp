// The guest D3D skip decision (guest_d3d_skip.h).

#include <cstdint>
#include <cstdio>

#include "hooks/guest_d3d_skip.h"

namespace {

using torchlight::hooks::SkipGuestD3D;
using torchlight::hooks::SkippableSlot;

int failures = 0;

void Check(bool ok, const char* what, uint32_t value = 0) {
  if (ok) return;
  ++failures;
  std::fprintf(stderr, "FAIL: %s (%u)\n", what, value);
}

}  // namespace

int main() {
  // Exactly the slots of step a; every other slot of the table (0-126) runs the guest's code.
  for (uint32_t slot = 0; slot < 127; ++slot) {
    bool want = slot == 44 || slot == 46 || slot == 47 || slot == 49;
    Check(SkippableSlot(slot) == want, "skippable slot", slot);
  }
  // The dispatcher and the member writer stay, by the evidence.
  Check(!SkippableSlot(45), "45 records through 44");
  Check(!SkippableSlot(43), "43 writes members");
  // Only the native mode with the cvar on: Xenos and parallel never skip.
  Check(SkipGuestD3D(true, true), "native, on");
  Check(!SkipGuestD3D(true, false), "native, off");
  Check(!SkipGuestD3D(false, true), "Xenos or parallel, on");
  Check(!SkipGuestD3D(false, false), "Xenos or parallel, off");
  static_assert(SkippableSlot(44) && !SkippableSlot(45));
  if (failures) {
    std::fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
  }
  std::puts("guest_d3d_skip_test: ok");
  return 0;
}
