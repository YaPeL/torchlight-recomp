// The guest's memory copies (guest_abi functions::kMemcpy, kLargeCopy) as host memcpy calls where
// that is exactly the same copy (hooks/guest_copy.h), else the guest's own. Every mode.
// --native_guest_copy=false keeps the guest's copies everywhere (docs/performance-profile.md).

#include <rex/cvar.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include "guest_abi/guest_functions.h"
#include "hooks/guest_copy.h"

REXCVAR_DEFINE_BOOL(native_guest_copy, true, "Torchlight",
                    "Run the guest's memory copies as host memcpy where that is the same copy "
                    "(false: the recompiled guest copies, as before)");

namespace {

namespace fn = torchlight::guest_abi::functions;

#define FUNCTION_ADDRESS_CHECK(entry, addr) \
  static_assert(fn::entry.address == 0x##addr##u, "guest_functions mismatch")

// Both copies take dst, src, size in r3, r4, r5 and return dst in r3, which the host copy leaves.
bool HostCopy(PPCContext& ctx, uint8_t* base) {
  return REXCVAR_GET(native_guest_copy) &&
         torchlight::hooks::HostGuestCopy(base, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32);
}

}  // namespace

extern "C" {

FUNCTION_ADDRESS_CHECK(kMemcpy, 82860A50);
REX_EXTERN(__imp__sub_82860A50);
REX_FUNC(sub_82860A50) {
  if (!HostCopy(ctx, base)) __imp__sub_82860A50(ctx, base);
}

FUNCTION_ADDRESS_CHECK(kLargeCopy, 821A7138);
REX_EXTERN(__imp__sub_821A7138);
REX_FUNC(sub_821A7138) {
  if (!HostCopy(ctx, base)) __imp__sub_821A7138(ctx, base);
}

}  // extern "C"
