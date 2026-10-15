// The pathfinder's neighbour opening (guest_abi/pathfinder.h, kOpenNeighbour) as host code
// (hooks/pathfinder_open.h): the same list walks, the same values, the same writes in the same
// order; the node comes from the game's allocator as before, and a closed node's improvement is
// propagated by the guest's own function. Nothing else changes, so it runs in every mode.
// --native_pathfinder_open=false keeps the recompiled function (docs/game-thread-parallelism.md,
// section 12).

#include "hooks/pathfinder_hooks.h"

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include "guest_abi/mods.h"
#include "guest_abi/ogre_layout.h"
#include "guest_abi/pathfinder.h"
#include "hooks/pathfinder_open.h"

REXCVAR_DEFINE_BOOL(native_pathfinder_open, true, "Torchlight",
                    "Run the pathfinder's neighbour opening as host code (hooks/pathfinder_open.h); "
                    "false runs the recompiled function");

namespace {

namespace abi = torchlight::guest_abi;
namespace pf = abi::pathfinder;

bool g_enabled = true;

}  // namespace

namespace torchlight::hooks {

void InstallPathfinderOpen() {
  g_enabled = REXCVAR_GET(native_pathfinder_open);
  REXLOG_INFO("pathfinder open: {} (--native_pathfinder_open={})",
              g_enabled ? "host code" : "the recompiled function", REXCVAR_GET(native_pathfinder_open));
}

}  // namespace torchlight::hooks

extern "C" {

REX_EXTERN(__imp__sub_8221FEB0);
REX_EXTERN(sub_821CD7F8);  // the game's allocator (mods.h kAlloc)
REX_EXTERN(sub_82220110);  // kImproveClosed
REX_FUNC(sub_8221FEB0) {
  static_assert(pf::kOpenNeighbour.address == 0x8221FEB0u && abi::mods::kAlloc.address == 0x821CD7F8u &&
                pf::kImproveClosed.address == 0x82220110u);
  if (!g_enabled) {
    __imp__sub_8221FEB0(ctx, base);
    return;
  }
  const uint32_t search = ctx.r3.u32, parent = ctx.r4.u32, x = ctx.r5.u32, y = ctx.r6.u32;
  const uint32_t goal_x = ctx.r7.u32, goal_y = ctx.r8.u32;
  // The guest's scalar FPU mode (flush to zero off), as the recompiled function sets it.
  ctx.fpscr.disableFlushMode();
  const torchlight::hooks::OpenPlan plan = torchlight::hooks::PlanOpenNeighbour(
      base, search, parent, x, y, goal_x, goal_y, abi::ReadF32(base, pf::kStepCost));
  uint32_t node = 0;
  if (plan.what == torchlight::hooks::OpenPlan::Case::kNew) {
    ctx.r3.u64 = 0;
    ctx.r4.u64 = pf::node::kSize.bytes;
    sub_821CD7F8(ctx, base);
    node = ctx.r3.u32;
    static const uint8_t zeros[pf::node::kSize.bytes] = {};
    abi::WriteBytes(base, node, zeros, sizeof(zeros));
    ctx.fpscr.disableFlushMode();
  }
  const uint32_t improved = torchlight::hooks::ApplyOpenPlan(base, search, parent, x, y, plan, node);
  if (improved) {
    ctx.r3.u64 = search;
    ctx.r4.u64 = improved;
    sub_82220110(ctx, base);
  }
}

}  // extern "C"
