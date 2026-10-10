// The guest D3D skip decision (guest_d3d_skip.h).

#include <cstdint>
#include <cstdio>
#include <initializer_list>

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
  // Exactly the slots of steps a and b; every other slot of the table (0-126) runs the guest's code.
  for (uint32_t slot = 0; slot < 127; ++slot) {
    bool want = slot == 44 || slot == 46 || slot == 47 || slot == 49 ||  // step a
                slot == 36 || slot == 37 || slot == 51 || slot == 53 || (slot >= 64 && slot <= 68) ||
                slot == 81 || slot == 84 ||  // step b
                slot == 50;                  // the texture matrix: computed, stored nowhere
    Check(SkippableSlot(slot) == want, "skippable slot", slot);
  }
  // The dispatcher and the member writer stay, by the evidence.
  Check(!SkippableSlot(45), "45 records through 44");
  Check(!SkippableSlot(43), "43 writes members");
  Check(!SkippableSlot(41) && !SkippableSlot(42), "41 and 42 write members only");
  // Render state writers not read for step b, and the culling mode the guest reads back (slot 62).
  for (uint32_t slot : {52u, 61u, 82u, 83u, 106u, 124u}) Check(!SkippableSlot(slot), "kept render state slot", slot);
  // Only the native mode with the cvar on: Xenos and parallel never skip.
  Check(SkipGuestD3D(true, true), "native, on");
  Check(!SkipGuestD3D(true, false), "native, off");
  Check(!SkipGuestD3D(false, true), "Xenos or parallel, on");
  Check(!SkipGuestD3D(false, false), "Xenos or parallel, off");
  static_assert(SkippableSlot(44) && !SkippableSlot(45));
  // The device calls: the draws and the bindings, nothing else of the device.
  using torchlight::hooks::SkippableDeviceCall;
  for (uint32_t address : {0x821CF830u, 0x821D0A10u, 0x821C39F8u, 0x821C3D58u})
    Check(SkippableDeviceCall(address), "skippable device call", address);
  // Kept: the render target and reference counting around the draw, the declaration, SetTexture,
  // the state flush used by other draws, the D3D9 _render itself.
  for (uint32_t address : {0x821C0A08u, 0x821E0088u, 0x821CEDD8u, 0x821BA040u, 0x821D1288u,
                           0x821BF940u, 0x821CE588u, 0x821CEB60u, 0x821CFF38u, 0x821C4058u})
    Check(!SkippableDeviceCall(address), "kept device call", address);
  if (failures) {
    std::fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
  }
  std::puts("guest_d3d_skip_test: ok");
  return 0;
}
