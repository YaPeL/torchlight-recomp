// The game's A* pathfinder as its neighbour-opening function reads it (0x8221FEB0, called from the
// search's expansion 0x8221FB00 for each passable neighbour; docs/game-thread-parallelism.md,
// section 3.B). The class name is unknown (no RTTI reaches it); the layout below is what that one
// function and 0x82220110 access, cited by instruction address. Same conventions as ogre_layout.h.

#pragma once

#include <cstdint>

#include "guest_abi/guest_functions.h"
#include "guest_abi/ogre_layout.h"

namespace torchlight::guest_abi::pathfinder {

using functions::GuestFunction;

// [confirmed] Opens the cell (x, y) as a neighbour of `parent` in the search `this`, toward the
// goal: r3 this, r4 parent, r5 x, r6 y, r7 goal x, r8 goal y (the caller @0x8221FB74..0x8221FB90).
// Finds the cell in the open list, else in the closed list, else makes a node (the game's
// allocator, 64 bytes, zeroed) and inserts it in the open list before the first node whose f is
// not below its own; then takes the parent's first free child slot, and makes the new node the
// search's best when its h is below the best's. hooks/pathfinder_hooks.cpp runs it as host code.
inline constexpr GuestFunction kOpenNeighbour{0x8221FEB0, Confidence::kConfirmed};
// [confirmed] When a closed node gets a lower g through the new parent, the guest calls this with
// (this, node): it pushes the lower g down the node's children (@0x822200F8).
inline constexpr GuestFunction kImproveClosed{0x82220110, Confidence::kConfirmed};
// [confirmed] The CRT memset (r3 destination, r4 value, r5 size) the new node is zeroed with
// (@0x8221FF54..0x8221FF64, after kAlloc with r3 0, r4 64: mods.h).
inline constexpr GuestFunction kMemset{0x82861140, Confidence::kConfirmed};
// [confirmed] The cost of one step, a rodata float added to the parent's g (r30 + 64 @0x8221FEE0;
// r30 = 0x820AA2C0).
inline constexpr uint32_t kStepCost = 0x820AA300;

// The search object (this).
// [confirmed] The open node with the lowest h seen (@0x82220014..0x82220030).
inline constexpr Field kBest{0x08, Confidence::kConfirmed};
// [confirmed] Sentinel nodes whose `next` starts the open and the closed lists (@0x8221FEC8,
// 0x8221FF20).
inline constexpr Field kOpenHead{0x0C, Confidence::kConfirmed};
inline constexpr Field kClosedHead{0x10, Confidence::kConfirmed};
// [confirmed] Cells per row: a cell's index is x + y * width (@0x8221FEDC, 0x8221FEF8).
inline constexpr Field kWidth{0x24, Confidence::kConfirmed};

// A node: 64 bytes from the game's allocator, zeroed, then filled (@0x8221FF6C..0x8221FFBC).
namespace node {
inline constexpr Field kF{0x00, Confidence::kConfirmed};  // g + h
inline constexpr Field kH{0x04, Confidence::kConfirmed};  // the squared distance to the goal
inline constexpr Field kG{0x08, Confidence::kConfirmed};  // the cost from the start
inline constexpr Field kX{0x0C, Confidence::kConfirmed};
inline constexpr Field kY{0x10, Confidence::kConfirmed};
inline constexpr Field kCell{0x14, Confidence::kConfirmed};
inline constexpr Field kParent{0x18, Confidence::kConfirmed};
// [confirmed] Eight child slots; the first zero one takes a new child (@0x8221FFF8..0x82220014).
// With all eight taken the guest stores at slot 8, which is `next` (@0x82220034..0x8222003C);
// the host version does the same.
inline constexpr Field kChildren{0x1C, Confidence::kConfirmed};
inline constexpr uint32_t kChildSlots = 8;
inline constexpr Field kNext{0x3C, Confidence::kConfirmed};
inline constexpr Size kSize{0x40, Confidence::kConfirmed};
}  // namespace node

}  // namespace torchlight::guest_abi::pathfinder
