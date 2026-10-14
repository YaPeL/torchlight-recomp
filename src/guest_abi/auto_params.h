// The guest GpuProgramParameters::_updateAutoParams, as hooks/auto_params_hooks.cpp replays it:
// the automatic constant list, the data source's getters and the writes into the float constant
// buffer. Same conventions as ogre_layout.h. Evidence: the recompiled 0x82201EB0 (its switch's
// jump table at 0x82201F58, 126 cases, OgreGpuProgramParams.h AutoConstantType order) and the
// write helpers it calls.

#pragma once

#include <cstdint>

#include "guest_abi/ogre_layout.h"

namespace torchlight::guest_abi::auto_params {

// [confirmed] GpuProgramParameters::_updateAutoParams(source, mask): r3 this, r4 the
// AutoParamDataSource, r5 the variability mask (16 bits used).
inline constexpr uint32_t kUpdateAutoParams = 0x82201EB0;

namespace params {
// [confirmed] mFloatConstants' data pointer at +0: every write adds physicalIndex * 4 to it
// (@0x82203C74 `lwz r11,0(r29)`, @0x82467E24).
inline constexpr Field kFloatConstants{0x00, Confidence::kConfirmed};
// [confirmed] mAutoConstants (vector) begin/end at +80/+84 (@0x82201EC4, @0x82201ECC, the loop's
// end test @0x82204240); the entries are 20 bytes (`addi r31,r31,20`).
inline constexpr Field kAutoConstantsBegin{0x50, Confidence::kConfirmed};
inline constexpr Field kAutoConstantsEnd{0x54, Confidence::kConfirmed};
// [confirmed] mCombinedVariability (u16) at +96: nothing to do when it has no bit of the mask
// (@0x82201EE4); mTransposeMatrices (byte) at +98: the matrix writes transpose when set
// (0x821C2868 @0x821C2874, 0x82467CC8 @0x82467CDC); mActivePassIterationIndex at +100, set to
// -1 at the start (@0x82201EF8).
inline constexpr Field kCombinedVariability{0x60, Confidence::kConfirmed};
inline constexpr Field kTransposeMatrices{0x62, Confidence::kConfirmed};
inline constexpr Field kActivePassIterationIndex{0x64, Confidence::kConfirmed};
}  // namespace params

namespace entry {
// [confirmed] AutoConstantEntry: paramType +0 (the switch index), physicalIndex +4, elementCount
// +8, data +12 (the light index or texture unit), variability +16 (u16, tested against the mask
// @0x82201F24).
inline constexpr Field kType{0x00, Confidence::kConfirmed};
inline constexpr Field kPhysicalIndex{0x04, Confidence::kConfirmed};
inline constexpr Field kElementCount{0x08, Confidence::kConfirmed};
inline constexpr Field kData{0x0C, Confidence::kConfirmed};
inline constexpr Field kVariability{0x10, Confidence::kConfirmed};
inline constexpr uint32_t kSize = 20;
inline constexpr uint32_t kTypeCount = 126;
}  // namespace entry

// AutoConstantType values (OgreGpuProgramParams.h:1.7.0 order, the switch's indices) the game's
// RTSS programs use (docs/game-thread-parallelism.md, section 11), with the data source's vtable
// slot each case calls (byte offset / 4 in the recompiled case) and how the result comes back.
enum class Act : uint32_t {
  kWorldMatrix = 0,                      // slot 11, Matrix4 by reference
  kInverseWorldMatrix = 1,               // slot 19
  kInverseTransposeWorldMatrix = 3,      // slot 22
  kWorldMatrixArray3x4 = 4,              // slots 12 (array) and 13 (count): 12 floats per matrix, untransposed
  kViewMatrix = 6,                       // slot 14
  kViewProjMatrix = 14,                  // slot 15
  kInverseTransposeWorldViewMatrix = 21, // slot 23
  kWorldViewProjMatrix = 22,             // slot 17
  kSurfaceDiffuseColour = 31,            // slot 44, ColourValue by reference
  kSurfaceSpecularColour = 32,           // slot 45
  kSurfaceEmissiveColour = 33,           // slot 46
  kSurfaceShininess = 34,                // slot 47, float in f1
  kLightDiffuseColour = 37,              // slot 31 (index), ColourValue by reference
  kLightPositionViewSpace = 43,          // slot 36 (index) Vector4 by value, slot 14, then 0x823FDEF8
  kDerivedAmbientLightColour = 65,       // slot 48, ColourValue by value
  kDerivedSceneColour = 66,              // slot 49, by value
  kDerivedLightDiffuseColour = 67,       // slot 44 by reference times slot 33 (index) by value
  kTextureViewProjMatrix = 76,           // slot 53 (data), Matrix4 by reference
  kTextureMatrix = 122,                  // slot 57 (data), Matrix4 by reference
};

namespace source {
inline constexpr VtableSlot kWorldMatrix{11, Confidence::kConfirmed};
inline constexpr VtableSlot kWorldMatrixArray{12, Confidence::kConfirmed};
inline constexpr VtableSlot kWorldMatrixCount{13, Confidence::kConfirmed};
inline constexpr VtableSlot kViewMatrix{14, Confidence::kConfirmed};
inline constexpr VtableSlot kViewProjMatrix{15, Confidence::kConfirmed};
inline constexpr VtableSlot kWorldViewProjMatrix{17, Confidence::kConfirmed};
inline constexpr VtableSlot kInverseWorldMatrix{19, Confidence::kConfirmed};
inline constexpr VtableSlot kInverseTransposeWorldMatrix{22, Confidence::kConfirmed};
inline constexpr VtableSlot kInverseTransposeWorldViewMatrix{23, Confidence::kConfirmed};
inline constexpr VtableSlot kLightDiffuseColour{31, Confidence::kConfirmed};
inline constexpr VtableSlot kLightDiffuseColourWithPower{33, Confidence::kConfirmed};
inline constexpr VtableSlot kLightAs4DVector{36, Confidence::kConfirmed};
inline constexpr VtableSlot kSurfaceDiffuseColour{44, Confidence::kConfirmed};
inline constexpr VtableSlot kSurfaceSpecularColour{45, Confidence::kConfirmed};
inline constexpr VtableSlot kSurfaceEmissiveColour{46, Confidence::kConfirmed};
inline constexpr VtableSlot kSurfaceShininess{47, Confidence::kConfirmed};
inline constexpr VtableSlot kDerivedAmbientLightColour{48, Confidence::kConfirmed};
inline constexpr VtableSlot kDerivedSceneColour{49, Confidence::kConfirmed};
inline constexpr VtableSlot kTextureViewProjMatrix{53, Confidence::kConfirmed};
inline constexpr VtableSlot kTextureTransformMatrix{57, Confidence::kConfirmed};
}  // namespace source

// [confirmed] Matrix4 * Vector4 (r3 result slot, r4 matrix, r5 vector; fused multiply-adds), the
// light position's transform to view space (@0x82203768).
inline constexpr uint32_t kMatrixTimesVector4 = 0x823FDEF8;

// [confirmed] The guest's frame: 2240 bytes below the caller's stack pointer (`stwu r1,-2240(r1)`
// @0x82201EBC); the by-value results land inside it (r1 + 592, 768, 784, 848, 960).
inline constexpr uint32_t kFrameBytes = 2240;

}  // namespace torchlight::guest_abi::auto_params
