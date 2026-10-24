// The hero's place, facing and speed, read for the development replay's trace (dev/pad_replay.h).
// Same conventions and evidence style as the other guest_abi headers.

#pragma once

#include <cstdint>

#include "guest_abi/game_ui.h"
#include "guest_abi/guest_functions.h"
#include "guest_abi/ogre_layout.h"

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

// [confirmed] A unit's scene node and its own position: the unit position getter sub_821D3F38
// (r4 = unit, r5 = world) asks the node at +0x58 for its derived position (vtable +0xC0, slot 48,
// Node::_getDerivedPosition in the 1.7.0 header order) when +0x58 is set and +0x50 is not 0
// (@0x821D3F58..@0x821D3F80); otherwise it returns the unit's own Vector3 at +0x70
// (@0x821D3FA4). The hero's +0x50 is 0 (a trace run in town), so the getter gives its +0x70; the
// trace takes the node's world transform anyway, as the PC trace does, and logs both once.
inline constexpr Field kUnitNode{0x58, Confidence::kConfirmed};
inline constexpr Field kUnitPosition{0x70, Confidence::kConfirmed};

// [confirmed] The unit update sub_821D5410 stores both at its end (@0x821D79BC +0x1F8, @0x821D79C0
// +0x1F4). Named as the PC trace names them (the speed scale, the speed); their meaning is not
// needed here, only the same fields on both builds.
inline constexpr Field kSpeedScale{0x1F4, Confidence::kConfirmed};
inline constexpr Field kSpeed{0x1F8, Confidence::kConfirmed};

// [confirmed] The guest Node's own and derived (world) transform, as its getters return them:
// getOrientation (vtable slot 7, 0x821FD860) returns +0x68; _getDerivedOrientation (slot 47,
// 0x821C9BB0) and _getDerivedPosition (slot 48, 0x821C8518) first update from the parent when
// +0x48 (mNeedParentUpdate) is set, through slot 1, then return +0x94 and +0xA4. _getFullTransform
// (slot 50, 0x821D3AF8) calls slots 47, 49 and 48 for its matrix; slot 49 (_getDerivedScale) is
// taken as the Vector3 after the derived position, +0xB0, in the 1.7.0 header order. Slots read in
// the node vtables at 0x82003A44 and 0x82003B5C. Quaternions are w, x, y, z.
inline constexpr Field kNodeOrientation{0x68, Confidence::kConfirmed};
inline constexpr Field kNodeNeedParentUpdate{0x48, Confidence::kConfirmed};
inline constexpr Field kNodeDerivedOrientation{0x94, Confidence::kConfirmed};
inline constexpr Field kNodeDerivedPosition{0xA4, Confidence::kConfirmed};
inline constexpr Field kNodeDerivedScale{0xB0, Confidence::kInferred};

// The stick's turn by the camera, in the hero's input handler sub_82199E08 (r31 = the client, r30 =
// the constant pool 0x820AA2C0):
// - [confirmed] the camera: client +0x10, then +8 (@0x82199EC8, @0x82199EE0); its orientation
//   (w, x, y, z) at +0x410 (@0x82199EF0), taken as Camera::mOrientation (the 1.7.0 header's first
//   Quaternion member; the Vector3 after it, mPosition, at +0x420 is [inferred]);
// - [confirmed] the camera's forward: sub_821C9BF8 (Quaternion * Vector3) of that orientation and
//   minus the Vector3 at 0x8349D4EC (0, 0, 1 in the image) (@0x82199ED0..@0x82199F10);
// - [confirmed] the angle: sub_821BEBE8 (atan2) of the forward's x and z (@0x82199F14..@0x82199F1C),
//   then x pool +0x27C (57.2958) - pool +0xB4 (90) (@0x82199F54) in degrees, x 0x8356C9DC (pi/180)
//   (@0x82199F58) into the stick's rotation.
inline constexpr Field kClientCameraHolder{0x10, Confidence::kConfirmed};
inline constexpr Field kCameraHolderCamera{0x8, Confidence::kConfirmed};
inline constexpr Field kCameraOrientation{0x410, Confidence::kConfirmed};
inline constexpr Field kCameraPosition{0x420, Confidence::kInferred};
inline constexpr uint32_t kUnitZ = 0x8349D4EC;
inline constexpr uint32_t kStickTurnScale = 0x820AA2C0 + 0x27C;
inline constexpr uint32_t kStickTurnOffset = 0x820AA2C0 + 0xB4;

// [confirmed] The stick the hero's input handler moves by: sub_82191018 (r3 = a float[2] it fills)
// reads the pad's left stick (int16 at +8, +10 of the game's XINPUT_STATE copy, 0x83557E80 + 16 *
// user) times pool +0x1E0 (1/32767) into x, y (@0x82191090..@0x8219109C), and returns with r3
// unchanged. sub_82199E08 calls it first (@0x82199E28, return address 0x82199E2C) and takes the
// vector's length against 0.1 (@0x82199E54).
inline constexpr functions::GuestFunction kReadStick{0x82191018, Confidence::kConfirmed};
inline constexpr uint32_t kHeroInputReadStickReturn = 0x82199E2C;

}  // namespace torchlight::guest_abi::hero_motion
