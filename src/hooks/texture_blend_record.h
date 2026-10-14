// Which fields of a guest LayerBlendModeEx its operation reads (hooks/render_system_hooks.cpp,
// slot 43). OGRE 1.7 never initializes alphaArg1, alphaArg2 and factor (TextureUnitState's
// constructor sets the types, operations and sources only), nor the colour arguments of an
// alpha-type mode, so those words are stack leftovers in the guest, different from run to run and
// with whatever ran before the call. Recording them as zero keeps the early out honest (12 % of the
// SetTextureBlend records were those leftovers changing) and makes two recordings of one scene
// comparable; the backend keys its programs on the colour and alpha arguments under the same
// conditions, and passes the factor to the RTSS as it comes. OgreBlendMode.h:139..160 and the
// D3D9 _setTextureBlendMode (OgreD3D9RenderSystem.cpp:2137..): the arguments are read only for
// LBS_MANUAL sources, the factor only for LBX_BLEND_MANUAL.

#pragma once

#include <cstdint>

#include "guest_abi/ogre_enums.h"

namespace torchlight::hooks {

struct TextureBlendFields {
  bool colour_args = false;  // colourArg1, colourArg2: a manual source and the colour type
  bool alpha_args = false;   // alphaArg1, alphaArg2: a manual source and the alpha type
  bool factor = false;       // factor: the LBX_BLEND_MANUAL operation
};

constexpr TextureBlendFields TextureBlendRecordedFields(uint32_t blend_type, uint32_t operation,
                                                        uint32_t source1, uint32_t source2) {
  namespace e = torchlight::guest_abi::ogre;
  const bool manual = source1 == e::layer_blend_source::kManual.value ||
                      source2 == e::layer_blend_source::kManual.value;
  return {manual && blend_type == e::layer_blend_type::kColour.value,
          manual && blend_type == e::layer_blend_type::kAlpha.value,
          operation == e::layer_blend_operation_ex::kBlendManual.value};
}

}  // namespace torchlight::hooks
