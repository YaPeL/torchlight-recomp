// The guest's memory copies (guest_abi functions::kMemcpy, kLargeCopy, kVectorCopy) as host memcpy
// calls where that is exactly the same copy (hooks/guest_copy.h), else the guest's own. Every mode.
// --native_guest_copy=false keeps the guest's memcpy and large copy (docs/performance-profile.md),
// --native_vector_copy=false the XDK vector copy the constant writers use.

#include "hooks/guest_copy_hooks.h"

#include <atomic>
#include <cstring>
#include <thread>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include "guest_abi/guest_functions.h"
#include "guest_abi/ogre_layout.h"
#include "hooks/guest_copy.h"

REXCVAR_DEFINE_BOOL(native_guest_copy, true, "Torchlight",
                    "Run the guest's memory copies as host memcpy where that is the same copy "
                    "(false: the recompiled guest copies, as before)");

REXCVAR_DEFINE_BOOL(native_vector_copy, true, "Torchlight",
                    "Run the XDK vector copy (the constant writers' copy) as host memcpy where "
                    "that is the same copy (false: the recompiled guest copy)");
#if TORCHLIGHT_DIAGNOSTICS
REXCVAR_DEFINE_BOOL(diag_vector_copy_check, false, "Torchlight",
                    "Diagnostics: run every routed vector copy both ways (the guest's, then the "
                    "host's over the restored destination) and log differences");
#endif

namespace {

namespace abi = torchlight::guest_abi;
namespace fn = torchlight::guest_abi::functions;

#define FUNCTION_ADDRESS_CHECK(entry, addr) \
  static_assert(fn::entry.address == 0x##addr##u, "guest_functions mismatch")

// Both copies take dst, src, size in r3, r4, r5 and return dst in r3, which the host copy leaves.
bool HostCopy(PPCContext& ctx, uint8_t* base) {
  return REXCVAR_GET(native_guest_copy) &&
         torchlight::hooks::HostGuestCopy(base, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32);
}

// The vector copy routes only while its byte loop's step word is 1 (static data: read once).
bool VectorCopyEnabled(const uint8_t* base) {
  static const bool enabled = [base] {
    if (!REXCVAR_GET(native_vector_copy)) return false;
    const uint32_t step = abi::ReadU32(base, fn::kVectorCopyStep);
    if (step != 1)
      REXLOG_WARN("vector copy: the guest's step word is {} (not 1); the guest copy runs", step);
    return step == 1;
  }();
  return enabled;
}

}  // namespace

extern "C" {
REX_EXTERN(__imp__sub_821C28D8);
}

namespace {

#if TORCHLIGHT_DIAGNOSTICS
// --diag_vector_copy_check: the guest's copy, then the host's over the restored destination. The
// bytes of the 128-byte lines around the destination but outside it are compared with their state
// before (a dcbzl outside the destination would show) and never written back, so only the
// destination is restored. On the first thread to copy only (the render thread in practice), so
// the counters and logs stay consistent; another thread writing the destination itself between the
// two copies would be undone, which nothing does for a constant buffer being filled.
bool CheckedCopy(PPCContext& ctx, uint8_t* base, uint32_t dst, uint32_t src, uint32_t size) {
  static std::atomic<uint64_t> checked{0}, differing{0}, outside{0}, unchecked{0};
  static const std::thread::id first_thread = std::this_thread::get_id();
  if (std::this_thread::get_id() != first_thread) return false;
  if (size > (1u << 20)) {
    if (unchecked.fetch_add(1, std::memory_order_relaxed) == 0)
      REXLOG_INFO("vector copy check: copies over 1 MiB are not checked");
    return false;
  }
  const uint32_t first = dst & ~127u;
  const uint32_t span =
      static_cast<uint32_t>(((uint64_t{dst} + size + 127u) & ~uint64_t{127}) - first);
  const uint32_t head = dst - first;
  uint8_t* lines = abi::xbox_memory::HostAddress(base, first);
  uint8_t* out = lines + head;
  std::vector<uint8_t> before(lines, lines + span);
  __imp__sub_821C28D8(ctx, base);
  if (std::memcmp(lines, before.data(), head) != 0 ||
      std::memcmp(out + size, before.data() + head + size, span - head - size) != 0) {
    if (outside.fetch_add(1, std::memory_order_relaxed) < 20)
      REXLOG_WARN("vector copy check: {:08X} <- {:08X} ({} bytes): the guest's copy changed "
                  "bytes outside the destination",
                  dst, src, size);
  }
  std::vector<uint8_t> guest(out, out + size);
  std::memcpy(out, before.data() + head, size);
  torchlight::hooks::HostGuestCopy(base, dst, src, size);
  const uint64_t n = checked.fetch_add(1, std::memory_order_relaxed) + 1;
  if (std::memcmp(out, guest.data(), size) != 0) {
    if (differing.fetch_add(1, std::memory_order_relaxed) < 20)
      REXLOG_WARN("vector copy check: {:08X} <- {:08X} ({} bytes) differs from the guest's copy",
                  dst, src, size);
    std::memcpy(out, guest.data(), size);
  }
  if (n % 1000000 == 0)
    REXLOG_INFO("vector copy check: {} copies compared, {} differ, {} wrote outside, {} over 1 MiB "
                "not checked",
                n, differing.load(std::memory_order_relaxed),
                outside.load(std::memory_order_relaxed), unchecked.load(std::memory_order_relaxed));
  ctx.r3.u64 = dst;
  return true;
}
#endif

}  // namespace

namespace torchlight::hooks {

void LogGuestCopyMode() {
  REXLOG_INFO("guest copies: {} (--native_guest_copy={}); vector copy: {} "
              "(--native_vector_copy={})",
              REXCVAR_GET(native_guest_copy) ? "host memcpy where it is the same copy"
                                             : "the recompiled guest code",
              REXCVAR_GET(native_guest_copy),
              REXCVAR_GET(native_vector_copy) ? "host memcpy where it is the same copy (a warning "
                                                "follows if the guest's step word is not 1)"
                                              : "the recompiled guest code",
              REXCVAR_GET(native_vector_copy));
}

}  // namespace torchlight::hooks

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

FUNCTION_ADDRESS_CHECK(kVectorCopy, 821C28D8);
REX_FUNC(sub_821C28D8) {
  const uint32_t dst = ctx.r3.u32, src = ctx.r4.u32, size = ctx.r5.u32;
  if (!VectorCopyEnabled(base)) return __imp__sub_821C28D8(ctx, base);
#if TORCHLIGHT_DIAGNOSTICS
  if (REXCVAR_GET(diag_vector_copy_check) && size &&
      torchlight::hooks::RouteGuestCopy(dst, src, size) ==
          torchlight::hooks::GuestCopyRoute::kHost &&
      CheckedCopy(ctx, base, dst, src, size)) {
    return;
  }
#endif
  if (!torchlight::hooks::HostGuestCopy(base, dst, src, size)) __imp__sub_821C28D8(ctx, base);
}

}  // extern "C"
