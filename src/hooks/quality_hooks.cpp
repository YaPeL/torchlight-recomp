// The graphics quality level (quality.h) written into the game's settings right after the game sets
// its fixed values (guest_abi game_settings::kAssignDefaults), before the game client creates its
// render textures. Takes effect from the start; changing it needs a restart.

#include "hooks/quality_hooks.h"

#include <string>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include "guest_abi/game_settings.h"
#include "guest_abi/ogre_layout.h"
#include "hooks/quality.h"

REXCVAR_DEFINE_STRING(quality, "normal", "Torchlight",
                      "Graphics quality from the game's own settings: normal, low or very_low "
                      "(hooks/quality.h); restart required");

namespace {

namespace abi = torchlight::guest_abi;
namespace gs = abi::game_settings;

torchlight::hooks::Quality g_quality = torchlight::hooks::Quality::kNormal;

// Writes int setting `index_global`'s value; false when its index is out of the values.
bool SetInt(uint8_t* base, uint32_t settings, uint32_t index_global, uint32_t value) {
  const uint32_t index = abi::ReadU32(base, index_global);
  const uint32_t begin = abi::ReadU32(base, settings + gs::kIntValuesBegin.offset);
  const uint32_t end = abi::ReadU32(base, settings + gs::kIntValuesEnd.offset);
  if (begin == 0 || end < begin || index >= (end - begin) / 4) return false;
  abi::WriteU32(base, begin + 4 * index, value);
  return true;
}

}  // namespace

namespace torchlight::hooks {

void InstallQuality() {
  const std::string name = REXCVAR_GET(quality);
  if (auto q = ParseQuality(name)) {
    g_quality = *q;
  } else {
    REXLOG_WARN("quality: unknown level '{}', normal", name);
  }
  REXLOG_INFO("quality: {} (--quality)", QualityName(g_quality));
}

}  // namespace torchlight::hooks

extern "C" {

REX_EXTERN(__imp__sub_823E1040);
REX_FUNC(sub_823E1040) {
  static_assert(gs::kAssignDefaults.address == 0x823E1040u);
  const uint32_t settings = ctx.r3.u32;
  __imp__sub_823E1040(ctx, base);
  if (auto resolution = torchlight::hooks::QualityShadowResolution(g_quality)) {
    if (SetInt(base, settings, gs::kShadowResolutionIndex, *resolution)) {
      REXLOG_INFO("quality: SHADOWRESOLUTION {}", *resolution);
    } else {
      REXLOG_WARN("quality: SHADOWRESOLUTION's index is out of the settings' values; left as is");
    }
  }
}

}  // extern "C"
