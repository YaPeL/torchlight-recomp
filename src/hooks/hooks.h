// Overrides of guest functions: RenderSystem slots, resource lifetime and the device swap.
//
// Each hook reads its arguments from the PPC context, hands neutral data to the capture session
// and calls the original (__imp__) function. The overrides are strong definitions of the weak
// sub_XXXXXXXX symbols the ReXGlue codegen emits, so they replace both vtable dispatch and direct
// calls without touching generated code or the SDK.

#pragma once

#include <cstdint>

namespace torchlight::hooks {

struct SlotAttribution {
  bool attributable;
  const char* note;  // why a slot is not counted on its own, or what the count includes
};

// Whether the call counter of a RenderSystem slot measures that slot alone.
SlotAttribution GetSlotAttribution(uint32_t slot);

}  // namespace torchlight::hooks
