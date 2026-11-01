// The hooks of the guest's hottest tree walks (hooks/guest_tree.h: literal translations, the same
// registers and stores as the recompiled functions, without the recompiled code's context
// traffic). Every mode. --native_tree_lookups=false runs the guest's.
//
// A diagnostics build checks them two ways:
// - --diag_tree_lookup_check runs the host walk (its stores recorded, not made) before the guest's
//   on every call and compares the registers r8-r11, every recorded store, and that the words the
//   walks may store to (r3, r1-16, r1-12) the host left alone are unchanged by the guest;
// - --diag_store_compare with --diag_store_compare_native compares the render thread's calls by the
//   store trace (diag/store_trace_api.h): memory other than the stack (the iterator or result at
//   r3), r1 and r3; the red zone and r8-r11 only the first check sees.

#include <atomic>
#include <cstdint>
#include <iterator>
#include <optional>
#include <utility>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include "guest_abi/guest_functions.h"
#include "guest_abi/ogre_layout.h"
#include "hooks/guest_tree.h"
#include "hooks/guest_tree_hooks.h"

#if TORCHLIGHT_DIAGNOSTICS
#include "diag/store_trace_api.h"
#endif

REXCVAR_DEFINE_BOOL(native_tree_lookups, true, "Torchlight",
                    "Run the guest's hottest tree walks (a std::map find and two tree iterator "
                    "increments) as host code (false: the recompiled guest functions)");
#if TORCHLIGHT_DIAGNOSTICS
REXCVAR_DEFINE_BOOL(diag_tree_lookup_check, false, "Torchlight",
                    "Diagnostics: run the host tree walks beside the guest's on every call and log "
                    "differences in registers and stores");
#endif

namespace {

namespace fn = torchlight::guest_abi::functions;
using namespace torchlight::hooks::guest_tree;

#define FUNCTION_ADDRESS_CHECK(entry, addr) \
  static_assert(fn::entry.address == 0x##addr##u, "guest_functions mismatch")

bool Enabled() {
  static const bool enabled = REXCVAR_GET(native_tree_lookups);
  return enabled;
}

template <Registers (*kWalk)(const PPCContext&, const uint8_t*, MemoryStores&&)>
void RunNative(PPCContext& ctx, uint8_t* base) {
  kWalk(ctx, base, MemoryStores{base}).Apply(ctx);
}

#if TORCHLIGHT_DIAGNOSTICS
// --diag_tree_lookup_check: the host walk first (stores recorded; it only reads the tree, which the
// guest's walk does not change; another thread changing it between the two walks would show as a
// false difference), then the guest's, then the registers, every recorded store's last value, and
// the words the walks can store to that the host left alone, against what the guest left.
template <Registers (*kWalk)(const PPCContext&, const uint8_t*, RecordedStores&)>
void CheckedCall(PPCContext& ctx, uint8_t* base, uint32_t entry,
                 void (*original)(PPCContext&, uint8_t*)) {
  static std::atomic<uint64_t> checked{0}, differing{0};
  const PPCContext before = ctx;
  const uint32_t targets[] = {ctx.r3.u32, ctx.r1.u32 - 16, ctx.r1.u32 - 12};
  uint32_t targets_before[std::size(targets)];
  for (size_t i = 0; i < std::size(targets); ++i) targets_before[i] = R32(base, targets[i]);
  RecordedStores recorded;
  const Registers host = kWalk(ctx, base, recorded);
  original(ctx, base);
  bool same = !recorded.overflow;
  auto reg = [&](const std::optional<uint32_t>& h, uint64_t guest_now, uint64_t guest_before) {
    same = same && (h ? uint64_t(*h) == guest_now : guest_now == guest_before);
  };
  reg(host.r8, ctx.r8.u64, before.r8.u64);
  reg(host.r9, ctx.r9.u64, before.r9.u64);
  reg(host.r10, ctx.r10.u64, before.r10.u64);
  reg(host.r11, ctx.r11.u64, before.r11.u64);
  for (uint32_t i = 0; i < recorded.count; ++i) {
    same = same && R32(base, recorded.stores[i].address) == recorded.stores[i].value;
  }
  for (size_t i = 0; i < std::size(targets); ++i) {  // a store the host left out
    bool stored = false;
    for (uint32_t j = 0; j < recorded.count; ++j) {
      stored = stored || recorded.stores[j].address == targets[i];
    }
    if (!stored) same = same && R32(base, targets[i]) == targets_before[i];
  }
  const uint64_t n = checked.fetch_add(1, std::memory_order_relaxed) + 1;
  if (!same && differing.fetch_add(1, std::memory_order_relaxed) < 20) {
    REXLOG_WARN("tree lookup check: {:08X} (r3 {:08X} r4 {:08X} r5 {:08X}) differs from the guest's: "
                "r8-r11 host {:08X} {:08X} {:08X} {:08X}, guest {:08X} {:08X} {:08X} {:08X}; {} stores, last "
                "{:08X}={:08X}, memory {:08X}",
                entry, before.r3.u32, before.r4.u32, before.r5.u32, host.r8.value_or(0),
                host.r9.value_or(0), host.r10.value_or(0), host.r11.value_or(0), ctx.r8.u32,
                ctx.r9.u32, ctx.r10.u32, ctx.r11.u32,
                recorded.count, recorded.count ? recorded.stores[recorded.count - 1].address : 0,
                recorded.count ? recorded.stores[recorded.count - 1].value : 0,
                recorded.count ? R32(base, recorded.stores[recorded.count - 1].address) : 0);
  }
  if (n % 10000000 == 0) {
    REXLOG_INFO("tree lookup check: {} calls compared, {} differ", n,
                differing.load(std::memory_order_relaxed));
  }
}
#endif

}  // namespace

namespace torchlight::hooks {

void LogTreeLookupMode() {
  REXLOG_INFO("tree lookups: {} (--native_tree_lookups={})",
              REXCVAR_GET(native_tree_lookups) ? "host code" : "the recompiled guest code",
              REXCVAR_GET(native_tree_lookups));
}

}  // namespace torchlight::hooks

extern "C" {

#if TORCHLIGHT_DIAGNOSTICS
#define TL_TREE_DIAG(addr, walk)                                                                \
  if (REXCVAR_GET(diag_tree_lookup_check)) {                                                    \
    return CheckedCall<&walk<RecordedStores&>>(ctx, base, 0x##addr##u, &__imp__sub_##addr);     \
  }                                                                                             \
  if (torchlight::diag::store_trace::CompareNativeCall(ctx, base, 0x##addr##u,                  \
                                                       &RunNative<&walk<MemoryStores>>,         \
                                                       &__imp__sub_##addr)) {                   \
    return;                                                                                     \
  }
#else
#define TL_TREE_DIAG(addr, walk)
#endif

#define TL_TREE_HOOK(entry, addr, walk)                                                         \
  FUNCTION_ADDRESS_CHECK(entry, addr);                                                          \
  REX_EXTERN(__imp__sub_##addr);                                                                \
  REX_FUNC(sub_##addr) {                                                                        \
    if (!Enabled()) return __imp__sub_##addr(ctx, base);                                        \
    TL_TREE_DIAG(addr, walk)                                                                    \
    RunNative<&walk<MemoryStores>>(ctx, base);                                                  \
  }

TL_TREE_HOOK(kDeviceMapFind, 82581D98, DeviceMapFind)
TL_TREE_HOOK(kTreeNext17, 824C6960, TreeNext17)
TL_TREE_HOOK(kTreeNext21, 8262D6C8, TreeNext21)

#undef TL_TREE_HOOK
#undef TL_TREE_DIAG

}  // extern "C"
