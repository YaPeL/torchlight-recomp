// The midasm hook AdaptiveVsync before `bl 0x82768610` @0x821FB1CC (guest_abi adaptive_vsync),
// declared by the generated sub_821FB108 as `AdaptiveVsync(ctx.r4);`, reached from both branches
// of the game's lastFPS test: r4 is the interval the game chose, rewritten by AdaptiveVsyncInterval
// under the host's fps cap (hooks/adaptive_vsync.h).

#include <rex/ppc/context.h>

#include "hooks/adaptive_vsync.h"

void AdaptiveVsync(PPCRegister& r4) {
  r4.u64 = torchlight::hooks::AdaptiveVsyncInterval(r4.u32, torchlight::hooks::AdaptiveVsyncCap());
}
