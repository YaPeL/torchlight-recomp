// The culling's Frustum::isVisible(box) (guest_abi static_geometry::frustum::kIsVisibleBox) as host
// code (hooks/frustum_test.h): the same reads, the same operations, the guest's result; its only
// effect is the bool the caller gets, so it runs in every mode. --native_frustum_test=false keeps
// the recompiled function (docs/game-thread-parallelism.md, section 12).

#include "hooks/frustum_hooks.h"

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include "guest_abi/ogre_layout.h"
#include "guest_abi/static_geometry.h"
#include "hooks/frustum_test.h"

REXCVAR_DEFINE_BOOL(native_frustum_test, true, "Torchlight",
                    "Run the culling's Frustum::isVisible(box) as host code (hooks/frustum_test.h); "
                    "false runs the recompiled function");

namespace {

namespace abi = torchlight::guest_abi;
namespace fr = abi::static_geometry::frustum;

bool g_enabled = true;

}  // namespace

namespace torchlight::hooks {

void InstallFrustumTest() {
  g_enabled = REXCVAR_GET(native_frustum_test);
  REXLOG_INFO("frustum test: {} (--native_frustum_test={})",
              g_enabled ? "host code" : "the recompiled function", REXCVAR_GET(native_frustum_test));
}

}  // namespace torchlight::hooks

extern "C" {

REX_EXTERN(__imp__sub_821C74A0);
REX_FUNC(sub_821C74A0) {
  static_assert(fr::kIsVisibleBox == 0x821C74A0u);
  if (!g_enabled) {
    __imp__sub_821C74A0(ctx, base);
    return;
  }
  const uint32_t frustum = ctx.r3.u32, box = ctx.r4.u32;
  bool visible;
  if (!torchlight::hooks::BoxExtentDecides(base, box, &visible)) {
    // The guest's FPU modes: scalar sums with flush to zero off, the vector test with it on (and
    // left on, as the guest leaves it).
    ctx.fpscr.disableFlushMode();
    const torchlight::hooks::BoxSums sums = torchlight::hooks::ComputeBoxSums(base, box);
    const torchlight::hooks::FrustumTestConstants constants{
        abi::ReadF32(base, fr::kBoxScale), abi::ReadF32(base, fr::kBoxSumW),
        abi::ReadF32(base, fr::kBoxDifferenceW)};
    ctx.fpscr.enableFlushModeUnconditional();
    visible = torchlight::hooks::PlanesAdmit(base, frustum, sums, constants);
  }
  ctx.r3.u64 = visible ? 1 : 0;
}

}  // extern "C"
