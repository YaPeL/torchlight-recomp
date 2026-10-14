// The RTSS per-object listener (guest_abi/rtss_listener.h) as host code (--native_rtss_listener,
// native mode only, on by default).
//
// For every object of every pass, ShaderGenerator::notifyRenderSingleObject looks the pass's
// SGPass up in a string-keyed std::map (UserObjectBindings::getUserAny), any_casts it through the
// RTTI, and calls each sub render state's updateGpuProgramsParams. The lookup and the cast are
// recompiled glue, about 1.5 % of the game's thread; here they are host reads of the same guest
// objects, and the sub render states are called as the guest called them (the same guest code,
// the same arguments, in order). Anything the lookup does not recognise (bindings not allocated
// yet, a holder of another type) runs the guest's own implementation.

#include <cstdint>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include "guest_abi/ogre_layout.h"
#include "guest_abi/rtss_listener.h"
#include "hooks/native_render_system.h"
#include "hooks/rtss_listener_hooks.h"

REXCVAR_DEFINE_BOOL(native_rtss_listener, true, "Torchlight",
                    "Native mode only: the RTSS per-object listener's SGPass lookup runs as host "
                    "code before calling the guest's sub render states (hooks/rtss_listener_hooks.cpp); "
                    "false runs the recompiled listener");

namespace {

namespace abi = torchlight::guest_abi;
namespace rl = torchlight::guest_abi::rtss_listener;
using torchlight::hooks::ListenerPass;

bool g_enabled = false;
struct Stats {
  uint64_t calls = 0, nothing = 0, guest = 0, sub_calls = 0, unresolved = 0;
} g_stats;
constexpr uint64_t kReportCalls = 2000000;

// The guest functions behind the sub render states' slot: a handful of targets, resolved once.
PPCFunc* Resolve(uint32_t target) {
  static uint32_t targets[16];
  static PPCFunc* functions[16];
  static unsigned count = 0;
  for (unsigned i = 0; i < count; ++i)
    if (targets[i] == target) return functions[i];
  PPCFunc* f = rex::runtime::ResolveIndirectFunction(target);
  if (f && count < 16) {
    targets[count] = target;
    functions[count++] = f;
  }
  return f;
}

}  // namespace

namespace torchlight::hooks {

void InstallRtssListener(bool native_only) {
  g_enabled = native_only && REXCVAR_GET(native_rtss_listener);
  REXLOG_INFO("RTSS listener: {} (--native_rtss_listener={}, native mode {})",
              g_enabled ? "SGPass lookup as host code" : "the recompiled listener",
              REXCVAR_GET(native_rtss_listener), native_only ? "on" : "off");
}

}  // namespace torchlight::hooks

extern "C" {

REX_EXTERN(__imp__sub_821C4F50);
REX_FUNC(sub_821C4F50) {
  static_assert(rl::kNotifyRenderSingleObject == 0x821C4F50u);
  if (!g_enabled) {
    __imp__sub_821C4F50(ctx, base);
    return;
  }
  if (++g_stats.calls % kReportCalls == 0) {
    REXLOG_INFO("RTSS listener: {} calls, {} without an SGPass, {} left to the guest, {} sub render "
                "state updates, {} unresolved targets",
                g_stats.calls, g_stats.nothing, g_stats.guest, g_stats.sub_calls, g_stats.unresolved);
  }
  const uint32_t self = ctx.r3.u32, renderable = ctx.r4.u32, pass = ctx.r5.u32;
  const uint32_t source = ctx.r6.u32, lights = ctx.r7.u32;
  const bool suppress = (ctx.r8.u32 & 0xFF) != 0;
  const uint32_t owner = abi::ReadU32(base, self + rl::kOwner.offset);
  if (!owner || !abi::ReadU8(base, owner + rl::kActiveViewportValid.offset)) return;
  const auto lookup = torchlight::hooks::LookupSgPass(base, pass, rl::kUserKey);
  if (lookup.what == ListenerPass::kGuest) {
    ++g_stats.guest;
    __imp__sub_821C4F50(ctx, base);
    return;
  }
  if (lookup.what == ListenerPass::kNothing) {
    ++g_stats.nothing;
    return;
  }
  const uint32_t state = abi::ReadU32(base, lookup.sg_pass + rl::kSgPassTargetRenderState.offset);
  if (!state || suppress) return;
  const uint32_t dst_pass = abi::ReadU32(base, lookup.sg_pass + rl::kSgPassDstPass.offset);
  const uint32_t end = abi::ReadU32(base, state + rl::kRenderStateSubStatesEnd.offset);
  for (uint32_t it = abi::ReadU32(base, state + rl::kRenderStateSubStatesBegin.offset);
       it && it < end; it += 4) {
    const uint32_t sub = abi::ReadU32(base, it);
    const uint32_t target = abi::ReadVirtual(base, sub, rl::kUpdateGpuProgramsParams);
    PPCFunc* function = Resolve(target);
    if (!function) {
      if (++g_stats.unresolved == 1)
        REXLOG_WARN("RTSS listener: no guest function at 0x{:08X} (sub render state 0x{:08X})", target, sub);
      continue;
    }
    ++g_stats.sub_calls;
    ctx.r3.u64 = sub;
    ctx.r4.u64 = renderable;
    ctx.r5.u64 = dst_pass;
    ctx.r6.u64 = source;
    ctx.r7.u64 = lights;
    function(ctx, base);
  }
}

}  // extern "C"
