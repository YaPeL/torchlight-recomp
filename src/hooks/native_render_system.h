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

// std::map<String, Any>::find for a key, on the guest's MSVC tree (ogre_layout stl_tree, the
// node layout of guest_abi/rtss_listener.h): the node holding `key`, or 0. Keys compare as
// std::string does (bytes, then length). Reads only.
uint32_t FindStringKey(const uint8_t* membase, uint32_t map, const char* key, uint32_t key_length);

// Reads the std::string at `address` (MSVC: inline buffer below 16 of capacity, else a pointer)
// into `out`, at most `capacity` bytes; returns its length (the whole length, even when cut).
uint32_t ReadGuestString(const uint8_t* membase, uint32_t address, char* out, uint32_t capacity);

// What the RTSS listener does with a pass, decided from the guest's objects: the SGPass whose
// render state updates the parameters, or why not. kGuest: a case left to the guest's code (it
// allocates or throws there).
enum class ListenerPass : uint8_t { kNothing, kGuest, kSgPass };
struct ListenerLookup {
  ListenerPass what = ListenerPass::kGuest;
  uint32_t sg_pass = 0;
};
// `key` is the guest address of the std::string SGPass::UserKey (rtss_listener::kUserKey).
ListenerLookup LookupSgPass(const uint8_t* membase, uint32_t pass, uint32_t key);

}  // namespace torchlight::hooks
