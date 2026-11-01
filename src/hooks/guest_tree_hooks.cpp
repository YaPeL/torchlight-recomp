// The guest's hottest tree walks (guest_abi functions::kDeviceMapFind, kTreeNext17, kTreeNext21) as
// host code: the same loads, stores and register results as the recompiled functions, without the
// recompiled code's context traffic. Every mode. --native_tree_lookups=false runs the guest's.
//
// Each is a literal translation of the function's instructions (listed with them in guest_abi): the
// registers it leaves (r8-r11; cr6 is a local of the generated code) and every store, the red zone
// ones included. A diagnostics build checks them two ways: --diag_tree_lookup_check runs the host
// code (its stores recorded, not made) before the guest's on every call and compares registers and
// stores; --diag_store_compare compares each call by the store trace (diag/store_trace_api.h).

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
#include "hooks/guest_copy_hooks.h"
#include "guest_abi/ogre_layout.h"

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

namespace abi = torchlight::guest_abi;
namespace fn = torchlight::guest_abi::functions;

#define FUNCTION_ADDRESS_CHECK(entry, addr) \
  static_assert(fn::entry.address == 0x##addr##u, "guest_functions mismatch")

uint32_t R32(const uint8_t* base, uint32_t address) { return abi::ReadU32(base, address); }
uint32_t R8(const uint8_t* base, uint32_t address) { return abi::ReadU8(base, address); }

// Where the host code's stores go: guest memory, or a record for the check.
struct MemoryStores {
  uint8_t* base;
  void operator()(uint32_t address, uint32_t value) const { abi::WriteU32(base, address, value); }
};
struct RecordedStores {
  struct Store {
    uint32_t address, value;
  };
  Store stores[16];
  uint32_t count = 0;
  bool overflow = false;
  void operator()(uint32_t address, uint32_t value) {
    for (uint32_t i = 0; i < count; ++i) {
      if (stores[i].address == address) {  // the last value per address (an iterator walking up)
        stores[i].value = value;
        return;
      }
    }
    if (count == std::size(stores)) {
      overflow = true;
      return;
    }
    stores[count++] = {address, value};
  }
};

// The registers a walk leaves; unset ones are left as they were.
struct Registers {
  std::optional<uint32_t> r8, r9, r10, r11;
  void Apply(PPCContext& ctx) const {
    if (r8) ctx.r8.u64 = *r8;
    if (r9) ctx.r9.u64 = *r9;
    if (r10) ctx.r10.u64 = *r10;
    if (r11) ctx.r11.u64 = *r11;
  }
};

// 0x82581D98: std::map<uint32_t, T>::find (MSVC tree: left +0, parent +4, right +8, key +12, isnil
// +21) into *r3, for the map r4 and the key at r5.
template <typename Stores>
Registers DeviceMapFind(const PPCContext& ctx, const uint8_t* base, Stores&& store) {
  Registers out;
  const uint32_t r1 = ctx.r1.u32, r3 = ctx.r3.u32, r4 = ctx.r4.u32, r5 = ctx.r5.u32;
  uint32_t r10 = R32(base, r4 + 4);               // lwz r10,4(r4): the head
  uint32_t r11 = R32(base, r10 + 4);              // lwz r11,4(r10): the root
  uint32_t r9 = R8(base, r11 + 21);               // lbz r9,21(r11)
  if (r9 == 0) {                                  // bne cr6,0x82581dd8
    r9 = R32(base, r5);                           // lwz r9,0(r5): the key
    uint32_t r8;
    do {
      r8 = R32(base, r11 + 12);                   // lwz r8,12(r11)
      if (r8 < r9) {                              // cmplw; bge cr6,0x82581dc4
        r11 = R32(base, r11 + 8);                 // lwz r11,8(r11)
      } else {
        r10 = r11;                                // mr r10,r11
        r11 = R32(base, r11);                     // lwz r11,0(r11)
      }
      r8 = R8(base, r11 + 21);                    // lbz r8,21(r11)
    } while (r8 == 0);                            // beq cr6,0x82581db0
    out.r8 = r8;
  }
  r11 = R32(base, r4 + 4);                        // lwz r11,4(r4)
  store(r1 - 16, r10);                            // stw r10,-16(r1)
  if (r10 != r11) {                               // beq cr6,0x82581e08
    r9 = R32(base, r5);                           // lwz r9,0(r5)
    const uint32_t r8 = R32(base, r10 + 12);      // lwz r8,12(r10)
    out.r8 = r8;
    if (!(r9 < r8)) {                             // blt cr6,0x82581e08
      r11 = r10;                                  // addi r11,r1,-16; lwz r11,0(r11)
      store(r3, r11);                             // stw r11,0(r3)
      out.r9 = r9, out.r10 = r10, out.r11 = r11;
      return out;
    }
  }
  store(r1 - 12, r11);                            // stw r11,-12(r1); addi; lwz r11,0(r11)
  store(r3, r11);                                 // stw r11,0(r3)
  out.r9 = r9, out.r10 = r10, out.r11 = r11;
  return out;
}

// 0x824C6960 (isnil +17) and 0x8262D6C8 (isnil +21): a tree iterator's operator++ on the iterator
// at r3 (MSVC tree: left +0, parent +4, right +8).
template <uint32_t kIsNil, typename Stores>
Registers TreeNext(const PPCContext& ctx, const uint8_t* base, Stores&& store) {
  Registers out;
  const uint32_t r3 = ctx.r3.u32;
  // The iterator's value as the walk leaves it: the guest reloads it after its own stores, which
  // the check's recorded stores do not make.
  uint32_t at_r3 = R32(base, r3);
  uint32_t r11 = at_r3;                           // lwz r11,0(r3)
  uint32_t r10 = R8(base, r11 + kIsNil);          // lbz r10,N(r11)
  out.r10 = r10, out.r11 = r11;
  if (r10 != 0) return out;                       // bnelr cr6
  r10 = R32(base, r11 + 8);                       // lwz r10,8(r11): the right child
  uint32_t r9 = R8(base, r10 + kIsNil);           // lbz r9,N(r10)
  if (r9 == 0) {                                  // bne cr6,+0x4c (up)
    r11 = R32(base, r10);                         // lwz r11,0(r10)
    r9 = R8(base, r11 + kIsNil);                  // lbz r9,N(r11)
    while (r9 == 0) {                             // bne cr6,+0x44 (store r10)
      r10 = r11;                                  // mr r10,r11
      r11 = R32(base, r11);                       // lwz r11,0(r11)
      r9 = R8(base, r11 + kIsNil);                // lbz r9,N(r11); beq cr6,+0x30
    }
    store(r3, r10);                               // stw r10,0(r3)
    out.r9 = r9, out.r10 = r10, out.r11 = r11;
    return out;
  }
  r11 = R32(base, r11 + 4);                       // lwz r11,4(r11): the parent
  r10 = R8(base, r11 + kIsNil);                   // lbz r10,N(r11)
  if (r10 == 0) {                                 // bne cr6,+0x84 (store r11)
    for (;;) {
      r10 = at_r3;                                // lwz r10,0(r3)
      r9 = R32(base, r11 + 8);                    // lwz r9,8(r11)
      if (r10 != r9) break;                       // bne cr6,+0x84
      store(r3, r11);                             // stw r11,0(r3); rotlwi r11,r11,0
      at_r3 = r11;
      r11 = R32(base, r11 + 4);                   // lwz r11,4(r11)
      r10 = R8(base, r11 + kIsNil);               // lbz r10,N(r11)
      if (r10 != 0) break;                        // beq cr6,+0x5c (loop)
    }
  }
  store(r3, r11);                                 // stw r11,0(r3)
  out.r9 = r9, out.r10 = r10, out.r11 = r11;
  return out;
}

template <typename Stores>
Registers TreeNext17(const PPCContext& ctx, const uint8_t* base, Stores&& store) {
  return TreeNext<17>(ctx, base, std::forward<Stores>(store));
}
template <typename Stores>
Registers TreeNext21(const PPCContext& ctx, const uint8_t* base, Stores&& store) {
  return TreeNext<21>(ctx, base, std::forward<Stores>(store));
}

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
// guest's walk does not change), then the guest's, then the registers and every recorded store's
// last value against what the guest left.
template <Registers (*kWalk)(const PPCContext&, const uint8_t*, RecordedStores&)>
void CheckedCall(PPCContext& ctx, uint8_t* base, uint32_t entry,
                 void (*original)(PPCContext&, uint8_t*)) {
  static std::atomic<uint64_t> checked{0}, differing{0};
  const PPCContext before = ctx;
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
  const uint64_t n = checked.fetch_add(1, std::memory_order_relaxed) + 1;
  if (!same && differing.fetch_add(1, std::memory_order_relaxed) < 20) {
    REXLOG_WARN("tree lookup check: {:08X} (r3 {:08X} r4 {:08X} r5 {:08X}) differs from the guest's: "
                "r9-r11 host {:08X} {:08X} {:08X}, guest {:08X} {:08X} {:08X}; {} stores, last "
                "{:08X}={:08X}, memory {:08X}",
                entry, before.r3.u32, before.r4.u32, before.r5.u32, host.r9.value_or(0),
                host.r10.value_or(0), host.r11.value_or(0), ctx.r9.u32, ctx.r10.u32, ctx.r11.u32,
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
