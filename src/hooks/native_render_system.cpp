#include "hooks/native_render_system.h"

#include "guest_abi/ogre_enums.h"
#include "guest_abi/ogre_layout.h"

namespace torchlight::hooks {

namespace abi = torchlight::guest_abi;

bool NativeSetTextureBlendMode(uint8_t* membase, uint32_t render_system, uint32_t unit,
                               uint32_t blend_mode) {
  namespace lb = abi::ogre::layer_blend_mode_ex;
  namespace rs = abi::ogre::render_system;
  const uint32_t type = abi::ReadU32(membase, blend_mode, lb::kBlendType);
  if (type == abi::ogre::layer_blend_type::kAlpha.value) return true;  // reads only
  if (type != abi::ogre::layer_blend_type::kColour.value) return false;
  const uint32_t dst = render_system + rs::kManualBlendColours.offset +
                       unit * rs::kManualBlendColoursUnitStride;
  // Two ColourValues (four floats each), copied word by word in the guest's byte order.
  for (uint32_t i = 0; i < 4; ++i) {
    abi::WriteU32(membase, dst + 4 * i, abi::ReadU32(membase, blend_mode + lb::kColourArg1.offset + 4 * i));
    abi::WriteU32(membase, dst + 16 + 4 * i,
                  abi::ReadU32(membase, blend_mode + lb::kColourArg2.offset + 4 * i));
  }
  return true;
}

}  // namespace torchlight::hooks
