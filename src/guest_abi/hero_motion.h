// The hero's place, facing and speed, read for the development replay's trace (dev/pad_replay.h).
// Same conventions and evidence style as the other guest_abi headers.

#pragma once

#include <cstdint>

#include "guest_abi/game_ui.h"
#include "guest_abi/guest_functions.h"
#include "guest_abi/ogre_layout.h"
#include "guest_abi/static_geometry.h"

namespace torchlight::guest_abi::hero_motion {

// [confirmed] The game client: the object the in-level chain ends at (functions::kInLevelChain from
// the CGameUI instance). Its caller sub_821EF318 takes it from +0x70 of its own object
// (@0x821EF584), tests the same +0x1404 == 1 (@0x821EF5B4) and then calls its frame update
// sub_821EBA48 with it in r3 (@0x821EF5D0).
inline uint32_t Client(const uint8_t* base) {
  uint32_t p = ReadU32(base, game_ui::kGameUiGlobal);
  for (uint32_t offset : functions::kInLevelChain) {
    if (!p) return 0;
    p = ReadU32(base, p + offset);
  }
  return p && ReadU32(base, p + functions::kInLevelFlag) == 1 ? p : 0;
}
// [confirmed] The client's hero unit: the frame update sub_821EBA48 reads it at +0x2C on each of
// its paths (@0x821EBB40, @0x821EC278, @0x821EC6EC...) and passes it as the unit to the hero's
// own updates (@0x821EC288).
inline constexpr Field kClientHero{0x2C, Confidence::kConfirmed};

// [confirmed] A unit's scene node and whether it is in the scene: the unit position getter
// sub_821D3F38 (r4 = unit, r5 = world) asks the node at +0x58 for its derived position (vtable
// +0xC0, slot 48, Node::_getDerivedPosition in the 1.7.0 header order) when +0x58 is set and +0x50
// is not 0 (@0x821D3F58..@0x821D3F80); otherwise it returns the unit's own position (+0x70).
inline constexpr Field kUnitNode{0x58, Confidence::kConfirmed};
inline constexpr Field kUnitInScene{0x50, Confidence::kConfirmed};

// [confirmed] The unit update sub_821D5410 stores both at its end (@0x821D79BC +0x1F8, @0x821D79C0
// +0x1F4). Named as the PC trace names them (the speed scale, the speed); their meaning is not
// needed here, only the same fields on both builds.
inline constexpr Field kSpeedScale{0x1F4, Confidence::kConfirmed};
inline constexpr Field kSpeed{0x1F8, Confidence::kConfirmed};

// The node's world transform: static_geometry::node::kCachedTransform (Matrix4, row-major, float
// [4][4]), valid while kCachedTransformOutOfDate is 0. Entry (row, col) at row * 16 + col * 4.
inline constexpr uint32_t MatrixEntry(int row, int col) {
  return static_geometry::node::kCachedTransform.offset + uint32_t(row * 16 + col * 4);
}

}  // namespace torchlight::guest_abi::hero_motion
