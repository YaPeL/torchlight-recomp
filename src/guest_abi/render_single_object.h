// SceneManager::renderSingleObject (OgreSceneManager.cpp:3052) as the native path of option B's
// stage 1 reads and calls it (hooks/native_render_object.cpp, docs/perf/native-render-queue.md).
// Same conventions as ogre_layout.h (big-endian, MSVC layout, confidence per entry). Lines are
// generated/default/torchlight_recomp.98.cpp's (sub_821C5FA0) unless another file is named.

#pragma once

#include <cstdint>

#include "guest_abi/ogre_layout.h"

namespace torchlight::guest_abi::render_single_object {

// renderSingleObject(this r3, rend r4, pass r5, lightScissoringClipping r6, doLightIteration r7,
// manualLightList r8). Frame of 320 bytes (stwu r1,-320(r1) @0x821C5FB0); its RenderOperation at
// r1+112, above the parameter home area (r1+0x10..0x4F) callees may write before their own stwu
// (sub_8256A270 stores r10 at 76(r1); vararg functions store 64(r1), 72(r1)). The native path
// builds the same frame.
inline constexpr uint32_t kRenderSingleObject = 0x821C5FA0;
inline constexpr uint32_t kFrameSize = 320;
inline constexpr uint32_t kRenderOperationInFrame = 112;

// The RenderOperation it builds on its stack at r1+112 (lines 395-412): vertexData +0 = 0,
// operationType +4 = 4 (OT_TRIANGLE_LIST), useIndexes +8 (byte) = 1, indexData +12 = 0,
// srcRenderable +16 = 0 then rend (@0x821C6008), and [Runic difference] a sixth word +20 = 1 (the
// instance count, render_operation::kRunicInstanceCount).
namespace render_operation {
inline constexpr uint32_t kSize = 24;
inline constexpr Field kVertexData{0x00, Confidence::kConfirmed};
inline constexpr Field kOperationType{0x04, Confidence::kConfirmed};
inline constexpr Field kUseIndexes{0x08, Confidence::kConfirmed};
inline constexpr Field kIndexData{0x0C, Confidence::kConfirmed};
inline constexpr Field kSrcRenderable{0x10, Confidence::kConfirmed};
inline constexpr Field kInstanceCount{0x14, Confidence::kRunicDifference};
inline constexpr uint32_t kTriangleList = 4;
}  // namespace render_operation

namespace scene_manager {
// [confirmed] mDestRenderSystem: every RS call loads +80 (lwz r3,80(r27), e.g. @0x821C63D0).
inline constexpr Field kRenderSystem{80, Confidence::kConfirmed};
// [confirmed] mCameraInProgress: the polygon mode clamp reads +164 then +1140 (@0x821C6444).
inline constexpr Field kCameraInProgress{164, Confidence::kConfirmed};
// [confirmed] mTempXform (Matrix4[256]): getWorldTransforms writes it, APDS setWorldMatrices and the
// determinant read it (addi r8,r11,412; addi r3,r27,412 @0x821C6330).
inline constexpr Field kTempXform{412, Confidence::kConfirmed};
// [confirmed] mNormaliseNormalsOnScale, mFlipCullingOnNegativeScale (bytes), mPassCullingMode
// (u32): lbz 16798 / 16799, lwz 16800 (lines 815-869).
inline constexpr Field kNormaliseNormalsOnScale{16798, Confidence::kConfirmed};
inline constexpr Field kFlipCullingOnNegativeScale{16799, Confidence::kConfirmed};
inline constexpr Field kPassCullingMode{16800, Confidence::kConfirmed};
// [confirmed] The identity view / projection resets pending (bytes resetViewProjMode 0x821C0700
// reads): with both clear, slots 7 and 8 write nothing for a renderable without identity flags.
inline constexpr Field kResetIdentityView{16796, Confidence::kConfirmed};
inline constexpr Field kResetIdentityProj{16797, Confidence::kConfirmed};
// [confirmed] mAutoParamDataSource: lwz r3,17128(r27) before APDS slots 1 and 2 (@0x821C61B8).
inline constexpr Field kAutoParamDataSource{17128, Confidence::kConfirmed};
// [confirmed] mSuppressRenderStateChanges: lbz 17177, a set byte skips to 0x821C6BB8.
inline constexpr Field kSuppressRenderStateChanges{17177, Confidence::kConfirmed};
// [confirmed] mCameraRelativeRendering: lbz 17240; set, renderSingleObject rewrites mTempXform's
// translations (lines 494-560).
inline constexpr Field kCameraRelativeRendering{17240, Confidence::kConfirmed};
// [confirmed] mGpuParamsDirty (u16): lhz / sth 17332, |= 2 (GPV_PER_OBJECT) on every call that
// gets past the view / projection mode (@0x821C6164).
inline constexpr Field kGpuParamsDirty{17332, Confidence::kConfirmed};
inline constexpr uint16_t kGpvPerObject = 2;
// OctreeSceneManager's vtable (render_queue.h) and the slots renderSingleObject calls, with the
// targets the native path requires (anything else falls back to the guest).
inline constexpr uint32_t kOctreeVtable = 0x82005B9C;
inline constexpr VtableSlot kUseRenderableViewProjMode{7, Confidence::kConfirmed};  // vt+28
inline constexpr uint32_t kUseRenderableViewProjModeTarget = 0x821CEE58;
inline constexpr VtableSlot kResetViewProjMode{8, Confidence::kConfirmed};  // vt+32 @0x821C6C68
inline constexpr uint32_t kResetViewProjModeTarget = 0x821C0700;
inline constexpr VtableSlot kFireRenderSingleObject{13, Confidence::kConfirmed};  // vt+52
inline constexpr uint32_t kFireRenderSingleObjectTarget = 0x821C4EE0;
inline constexpr VtableSlot kUseLightsGpuProgram{39, Confidence::kConfirmed};  // vt+156
inline constexpr uint32_t kUseLightsGpuProgramTarget = 0x821C12C0;
inline constexpr VtableSlot kUpdateGpuProgramParameters{41, Confidence::kConfirmed};  // vt+164
inline constexpr uint32_t kUpdateGpuProgramParametersTarget = 0x821C26D0;
}  // namespace scene_manager

namespace camera {
// [confirmed] mSceneDetail (PolygonMode, signed compare with the pass's, @0x821C6448).
inline constexpr Field kPolygonMode{1140, Confidence::kConfirmed};
}  // namespace camera

// D3D9RenderSystem's vtable and the slots on the path (render_system_hooks.cpp names them).
namespace render_system {
inline constexpr uint32_t kD3D9Vtable = 0x82005794;
inline constexpr VtableSlot kSetTextureUnitSettings{32, Confidence::kConfirmed};  // vt+128
inline constexpr VtableSlot kSetCullingMode{61, Confidence::kConfirmed};          // vt+244
inline constexpr VtableSlot kGetCullingMode{62, Confidence::kConfirmed};          // vt+248
inline constexpr VtableSlot kSetPolygonMode{81, Confidence::kConfirmed};          // vt+324
inline constexpr VtableSlot kSetNormaliseNormals{86, Confidence::kConfirmed};     // vt+344
// The shared empty function the D3D9 render system's slot 86 points to (no write, no hook).
inline constexpr uint32_t kEmptyFunction = 0x825BC790;
inline constexpr VtableSlot kRender{87, Confidence::kConfirmed};                  // vt+348
inline constexpr VtableSlot kSetPassIterationCount{112, Confidence::kConfirmed};  // vt+448
inline constexpr VtableSlot kSetDeriveDepthBias{113, Confidence::kConfirmed};     // vt+452
}  // namespace render_system

// Renderable virtuals (SubEntity 0x82001B0C, render_queue.h).
namespace renderable {
inline constexpr VtableSlot kGetRenderOperation{3, Confidence::kConfirmed};     // vt+12
inline constexpr VtableSlot kPreRender{4, Confidence::kConfirmed};              // vt+16
inline constexpr VtableSlot kPostRender{5, Confidence::kConfirmed};             // vt+20
inline constexpr VtableSlot kGetWorldTransforms{6, Confidence::kConfirmed};     // vt+24
inline constexpr VtableSlot kGetNumWorldTransforms{7, Confidence::kConfirmed};  // vt+28
inline constexpr VtableSlot kGetLights{9, Confidence::kConfirmed};              // vt+36
inline constexpr VtableSlot kGetPolygonModeOverrideable{13, Confidence::kConfirmed};  // vt+52
// [confirmed] mUseIdentityProjection / mUseIdentityView (bytes; lbz r20+26 @0x821C6098 for the
// view; slot 7 reads both).
inline constexpr Field kUseIdentityProjection{25, Confidence::kConfirmed};
inline constexpr Field kUseIdentityView{26, Confidence::kConfirmed};
}  // namespace renderable

namespace pass {
inline constexpr uint32_t kVtable = 0x82000C04;
// [confirmed] getPolygonModeOverrideable (vt+8, 0x821CEF78: lbz pass+229) and mPolygonMode (lwz
// +224, @0x821C6410).
inline constexpr VtableSlot kGetPolygonModeOverrideable{2, Confidence::kConfirmed};
inline constexpr Field kPolygonMode{224, Confidence::kConfirmed};
// [confirmed] mMaxSimultaneousLights (u16 +206) and mStartLight (u16 +208): with 8 and 0 the
// renderable's own light list is used and the static localLightList 0x83582BB4 is untouched
// (@0x821C6614..0x821C6630).
inline constexpr Field kMaxSimultaneousLights{206, Confidence::kConfirmed};
inline constexpr Field kStartLight{208, Confidence::kConfirmed};
inline constexpr uint16_t kMaxLights = 8;  // OGRE_MAX_SIMULTANEOUS_LIGHTS
}  // namespace pass

// The light lists (OgreCommon.h HashedVector<Light*>): begin +0, end +4, the dirty byte +20.
// Otherwise renderSingleObject copies the renderable's lights from the start light, at most the
// pass's maximum, into its static localLightList 0x83582BB4 (lines 1284-1383): clear
// (sub_8219D110) with r3 the list, the dirty byte set, insert (sub_8219CE00) with r3 the list's
// begin word, r4 the first light and r5 the end of the range, the dirty byte set again. A start
// light at or past the list's end skips the object (to the epilogue, 0x821C6C50). The list is
// built on first use under bit 0 of the guard word 0x83583058 (lines 1065-1090).
namespace light_list {
inline constexpr Field kBegin{0, Confidence::kConfirmed};
inline constexpr Field kEnd{4, Confidence::kConfirmed};
inline constexpr Field kDirty{20, Confidence::kConfirmed};
inline constexpr uint32_t kLocalLightList = 0x83582BB4;
inline constexpr uint32_t kLocalLightListGuard = 0x83583058;
inline constexpr uint32_t kClear = 0x8219D110;
inline constexpr uint32_t kInsert = 0x8219CE00;
}  // namespace light_list

namespace gpu_program {
// [confirmed] GpuProgramUsage::mProgram.pRep (lwz usage+16 @0x821C6020).
inline constexpr Field kUsageProgram{16, Confidence::kConfirmed};
// [confirmed] getPassSurfaceAndLightStates (vt+268) and getPassTransformStates (vt+276): every
// D3D9 program class points both to 0x828AD748, which returns false, so no world matrix reaches the
// render system and useLights is not called.
inline constexpr VtableSlot kGetPassSurfaceAndLightStates{67, Confidence::kConfirmed};
inline constexpr VtableSlot kGetPassTransformStates{69, Confidence::kConfirmed};
inline constexpr uint32_t kReturnFalse = 0x828AD748;
}  // namespace gpu_program

namespace auto_param_data_source {
inline constexpr VtableSlot kSetCurrentRenderable{1, Confidence::kConfirmed};  // vt+4 @0x821C61C4
inline constexpr VtableSlot kSetWorldMatrices{2, Confidence::kConfirmed};      // vt+8 @0x821C61E0
}  // namespace auto_param_data_source

namespace texture_unit_state {
// [confirmed] The effects multimap renderSingleObject scans for view-relative texture coordinate
// generation (lines 704-814; the map at +420, its find sub_821C5F18 in torchlight_recomp.182.cpp
// :330): the head node at +424, the root at head+4 and a node's _Isnil byte at +61. In an empty map
// the root is the head, whose _Isnil is set: no unit needs _setTextureUnitSettings.
inline constexpr Field kEffectsHead{424, Confidence::kConfirmed};
inline constexpr uint32_t kNodeLeft = 0;
inline constexpr uint32_t kNodeParent = 4;
inline constexpr uint32_t kNodeRight = 8;
inline constexpr uint32_t kNodeKey = 12;  // EffectType, compared signed
inline constexpr uint32_t kNodeIsNil = 61;
// The scan (lines 720-775): find(ET_ENVIRONMENT_MAP = 0) and, from that node to the end (every
// later key too: upstream's loop does not stop at the key's last entry), an effect whose subtype
// (node +20, TextureEffect::subtype) is ENV_REFLECTION = 2; else find(ET_PROJECTIVE_TEXTURE = 1).
// Either: RS slot 32 _setTextureUnitSettings(unit index, unit) (vt+128 @0x821C62B8). find is
// sub_821C5F18 (lower bound, then the key compared), the walk sub_821C1120 (the tree's increment,
// torchlight_recomp.104.cpp:285).
inline constexpr uint32_t kNodeSubtype = 20;
inline constexpr int32_t kEnvironmentMap = 0;
inline constexpr int32_t kProjectiveTexture = 1;
inline constexpr int32_t kEnvReflection = 2;
}  // namespace texture_unit_state

// The negative-scale test (lines 856-937): Matrix4::determinant of mTempXform[0] inlined over four
// calls of the MINOR helper (r3 the matrix, r4..r6 the columns; returns f1, keeps r3) with fused
// single-precision steps, compared with the float at 0x820AA304 (lfs f31,68(r9), 0.0; also the
// zero setDeriveDepthBias gets, @0x821C686C).
inline constexpr uint32_t kMatrixMinor = 0x8244EA10;
inline constexpr uint32_t kZeroFloat = 0x820AA304;
inline constexpr uint32_t kCullClockwise = 2;
inline constexpr uint32_t kCullAnticlockwise = 3;

// GpuProgramParameters::_updateAutoParams(this r3, source r4, mask r5) (OgreGpuProgramParams.cpp
// :1600; generated torchlight_recomp.163.cpp:847): nothing when the automatic constants are
// empty or the mask misses their combined variability; else the active pass iteration index
// (ogre_layout gpu_program_parameters) = -1 and, for each entry whose variability meets the mask
// and whose type is at most 125, the jump table at 0x82201F58. The cases transcribed (the others
// run the guest's function): a matrix from an AutoParamDataSource getter (r3 the source; r4 the
// entry's data for the indexed ones), written by _writeRawConstant(Matrix4) (0x821C2868:
// transposed when the parameters' flag is set, then min(element count, 16) floats to the float
// buffer at the entry's physical index); and ACT_WORLD_MATRIX_ARRAY_3x4 (4, @0x822033EC: the
// array, getter vt+48, then the count, vt+52; 48 bytes per matrix, source stride 64, no
// transpose). The getter offsets, read from each case's code (the pointer cases end in the shared
// tail @0x8220422C, the indexed ones go through @0x82204218):
namespace auto_params {
inline constexpr uint32_t kUpdateAutoParams = 0x82201EB0;
inline constexpr uint32_t kMaxType = 125;
inline constexpr uint32_t kWorldMatrixArray3x4 = 4;
inline constexpr uint32_t kArrayGetter = 48;       // getWorldMatrixArray
inline constexpr uint32_t kArrayCountGetter = 52;  // getWorldMatrixCount
inline constexpr uint32_t kArrayMatrixBytes = 48;
inline constexpr uint32_t kArraySourceStride = 64;
struct MatrixCase {
  uint32_t type;
  uint32_t getter;  // vtable byte offset
  bool indexed;     // the entry's data in r4
};
inline constexpr MatrixCase kMatrixCases[] = {
    {0, 44, false},   {1, 76, false},   {3, 88, false},   {6, 56, false},   {7, 84, false},
    {10, 64, false},  {14, 60, false},  {18, 72, false},  {19, 80, false},  {21, 92, false},
    {22, 68, false},  {76, 212, true},  {78, 216, true},  {81, 224, true},  {122, 228, true},
};
inline constexpr uint32_t kMatrixFloats = 16;
}  // namespace auto_params

}  // namespace torchlight::guest_abi::render_single_object
