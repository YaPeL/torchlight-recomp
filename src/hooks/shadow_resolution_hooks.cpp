// The size of the game's projector shadow textures (settings::HostSettings::shadow_resolution).
// The game reads SHADOWRESOLUTION once, when its projector setup creates the textures at startup;
// by then its own settings files are not readable yet (SAVE:\ is not mounted), so its first-start
// defaults have set 512, as on the 360. When the host setting asks for another size, the setup
// first sets it through the game's own setter (guest_abi game_settings), exactly as the defaults
// did, and the game creates both textures at that size. Two of 1024 x 1024 do not fit in the 360's
// EDRAM next to the frame: the guest D3D creates them aliased (video_mode_hooks.cpp), which needs
// the native renderer. 512 leaves the game alone. The call runs at the setup's entry, below its
// caller's stack pointer (GuestCall), and every register is back before the setup runs.

#include <cstdint>

#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include "game_menu/guest_call.h"
#include "guest_abi/guest_functions.h"
#include "hooks/video_mode_hooks.h"
#include "live/install.h"

namespace {

namespace abi = torchlight::guest_abi;
namespace gs = torchlight::guest_abi::functions::game_settings;

#define FUNCTION_ADDRESS_CHECK(entry, addr) \
  static_assert(gs::entry.address == 0x##addr##u, "guest_functions mismatch")

void SetShadowResolution(PPCContext& ctx, uint8_t* base, uint32_t table) {
  const uint32_t size = torchlight::live::StartupHostSettings().shadow_resolution;
  if (size == 512 || !table) return;
  if (!torchlight::hooks::AliasRenderTargets()) {
    REXLOG_ERROR("settings: shadow resolution {} needs the native renderer; the game keeps 512", size);
    return;
  }
  const uint32_t index = abi::ReadU32(base, gs::kShadowResolutionIndex);
  torchlight::game_menu::GuestCall call(ctx, base);
  const bool set = call.Call(gs::kSetInt.address, {table, index, size}) != 0;
  if (set) {
    REXLOG_INFO("settings: shadow resolution {}", size);
  } else {
    REXLOG_ERROR("settings: the game has no setting {} for the shadow resolution; it keeps its own",
                 index);
  }
}

}  // namespace

extern "C" {

FUNCTION_ADDRESS_CHECK(kProjectorSetup, 82210148);
REX_EXTERN(__imp__sub_82210148);
REX_FUNC(sub_82210148) {
  SetShadowResolution(ctx, base, ctx.r4.u32);
  __imp__sub_82210148(ctx, base);
}

}  // extern "C"
