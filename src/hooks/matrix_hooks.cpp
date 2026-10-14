// Matrix4::concatenate (guest_abi ogre::matrix4::kConcatenate) as host code (hooks/matrix_concat.h):
// the same 32 reads, the same operations in the same order, the same 16 writes; nothing else
// changes, so it runs in every mode. --native_matrix_concatenate=false keeps the recompiled function
// (docs/game-thread-parallelism.md, section 12).

#include "hooks/matrix_hooks.h"

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include "guest_abi/ogre_layout.h"
#include "hooks/matrix_concat.h"

REXCVAR_DEFINE_BOOL(native_matrix_concatenate, true, "Torchlight",
                    "Run OGRE's Matrix4::concatenate as host code (hooks/matrix_concat.h); false runs "
                    "the recompiled function");

namespace {

bool g_enabled = true;

}  // namespace

namespace torchlight::hooks {

void InstallMatrixConcatenate() {
  g_enabled = REXCVAR_GET(native_matrix_concatenate);
  REXLOG_INFO("matrix concatenate: {} (--native_matrix_concatenate={})",
              g_enabled ? "host code" : "the recompiled function", REXCVAR_GET(native_matrix_concatenate));
}

}  // namespace torchlight::hooks

extern "C" {

REX_EXTERN(__imp__sub_821C2CC8);
REX_FUNC(sub_821C2CC8) {
  static_assert(torchlight::guest_abi::ogre::matrix4::kConcatenate == 0x821C2CC8u);
  if (!g_enabled) {
    __imp__sub_821C2CC8(ctx, base);
    return;
  }
  // The guest's scalar FPU mode (flush to zero off), as the recompiled function sets it.
  ctx.fpscr.disableFlushMode();
  torchlight::hooks::ConcatenateMatrices(base, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32);
}

}  // extern "C"
