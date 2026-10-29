// The game's bouncing-arrow animator: every frame the HUD (sub_821A40E0, on its own object: the
// pointer array at +6244, count +6248, capacity +6252, 4 bytes an entry, `lwz r3,0(r11)` before
// `bl 0x821a41a0`) steps each entry's phase (sub_821A41A0: phase += velocity * dt, reversed past
// the limit) and moves its window to base + phase * amplitude with Window::setPosition. A moved
// window asks for a redraw of the whole GUI (Window::onMoved sets the window's and the System's
// redraw flags), so the arrows of panels that are not open (their entries are kept for the whole
// session) made CEGUI re-queue every quad every frame (hooks/gui_animator_hooks.cpp).

#pragma once

#include <cstdint>

#include "guest_abi/ogre_layout.h"

namespace torchlight::guest_abi::gui_animator {

// [confirmed] `bl 0x821a49c8` (Window::setPosition: r3 the window, r4 the UVector2 at r1+80)
// @0x821A4234 in sub_821A41A0; r3 is the entry's window (`lwz r3,8(r3)` at the function's start,
// not written before the call). The next instruction, @0x821A4238, reads only the entry (r31),
// the constants (r30) and f31.
inline constexpr uint32_t kSetPositionCall = 0x821A4234;
inline constexpr uint32_t kAfterSetPosition = 0x821A4238;

}  // namespace torchlight::guest_abi::gui_animator
