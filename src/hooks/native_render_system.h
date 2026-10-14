// Host implementations of guest RenderSystem functions whose only effect is on the render system's
// own members (docs/game-thread-parallelism.md, section 9.1). Each writes exactly the members the
// guest's implementation writes, in the guest's layout, and leaves anything else to the guest. Pure
// over a guest memory base, so they are tested on a fake memory.

#pragma once

#include <cstdint>

namespace torchlight::hooks {

// D3D9RenderSystem::_setTextureBlendMode(unit, LayerBlendModeEx) as Runic's build does it (slot 43,
// 0x821C9100): the colour blend type stores colourArg1 and colourArg2 in mManualBlendColours[unit];
// the alpha type stores nothing. Returns false for any other blend type (the guest throws there), so
// the caller runs the guest's implementation.
bool NativeSetTextureBlendMode(uint8_t* membase, uint32_t render_system, uint32_t unit,
                               uint32_t blend_mode);

}  // namespace torchlight::hooks
