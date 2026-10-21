// The game client's projector textures every other frame (projector_textures.h): the hook on
// D3D9RenderTexture::update (guest_abi kD3D9RenderTextureUpdate). With the option off, or for any
// other render texture, the guest's update runs as always.

#include "hooks/projector_texture_hooks.h"

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include "guest_abi/guest_functions.h"
#include "guest_abi/ogre_layout.h"
#include "hooks/projector_textures.h"

REXCVAR_DEFINE_BOOL(projector_textures_every_other_frame, false, "Torchlight",
                    "Update the shadow and light projector textures every other frame (a quality "
                    "option for slow devices; they lag a frame)");

namespace {

namespace abi = torchlight::guest_abi;
namespace rt = abi::ogre::render_target;

bool g_enabled = false;
torchlight::hooks::EveryOtherFrame g_alternation;
torchlight::hooks::ProjectorControl g_control;

torchlight::hooks::ProjectorTextureFacts ReadFacts(const uint8_t* base, uint32_t target) {
  torchlight::hooks::ProjectorTextureFacts f;
  f.width = abi::ReadU32(base, target + rt::kWidth.offset);
  f.height = abi::ReadU32(base, target + rt::kHeight.offset);
  const uint32_t begin = abi::ReadU32(base, target + rt::kListenersBegin.offset);
  const uint32_t end = abi::ReadU32(base, target + rt::kListenersEnd.offset);
  for (uint32_t it = begin; it < end && f.listener_count < 64; it += 4) {
    const uint32_t listener = abi::ReadU32(base, it);
    if (f.listener_count++ == 0) f.first_listener = listener;
    if (listener &&
        abi::ReadU32(base, listener) == abi::functions::kGameClientRenderTargetListenerVtable) {
      f.game_client_listener = true;
    }
  }
  return f;
}

}  // namespace

namespace torchlight::hooks {

void InstallProjectorTextures() {
  g_enabled = REXCVAR_GET(projector_textures_every_other_frame);
  REXLOG_INFO("projector textures: {} (--projector_textures_every_other_frame={})",
              g_enabled ? "every other frame" : "every frame", g_enabled);
}

}  // namespace torchlight::hooks

extern "C" {

REX_EXTERN(__imp__sub_82198608);
REX_FUNC(sub_82198608) {
  static_assert(abi::functions::kD3D9RenderTextureUpdate.address == 0x82198608u);
  if (g_enabled) {
    const uint32_t target = ctx.r3.u32;
    const torchlight::hooks::ProjectorTextureFacts facts = ReadFacts(base, target);
    if (auto note = g_control.Check(target, facts)) REXLOG_WARN("projector textures: {}", *note);
    // Skipped: no update, no listener calls, no swap (a render texture's is a no-op); r3 is not
    // read by the caller (the loop reloads its iterator).
    if (torchlight::hooks::IsProjectorTexture(facts) && !g_alternation.Run(target)) return;
  }
  __imp__sub_82198608(ctx, base);
}

}  // extern "C"
