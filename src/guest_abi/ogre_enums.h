// Guest (OGRE 1.7.0, Runic build) enum values.
//
// The host runs OGRE 14, so no enum is assumed equal across versions: anything crossing
// into commands/ or the host backend maps these values explicitly. Header citations are
// relative to ~/ogre-1.7.0/OgreMain/include. Confidence follows ogre_layout.h.

#pragma once

#include <cstdint>

#include "guest_abi/ogre_layout.h"

namespace torchlight::guest_abi::ogre {

struct EnumValue {
  uint32_t value;
  Confidence confidence;
};

// RenderOperation::OperationType. All [confirmed] by D3D9RenderSystem::_render 0x821C4058,
// which switches on op-1 (@0x821C40B8) and derives the primitive count per type: points = n,
// lines = n/2, line strip = n-1, triangles = n/3, strip/fan = n-2 (@0x821C40E4..@0x821C41DC).
// Strip and fan are told apart by the Xbox D3DPRIMITIVETYPE they map to (6 = strip, 5 = fan).
namespace operation_type {
inline constexpr EnumValue kPointList{1, Confidence::kConfirmed};      // OgreRenderOperation.h:49
inline constexpr EnumValue kLineList{2, Confidence::kConfirmed};       // :51
inline constexpr EnumValue kLineStrip{3, Confidence::kConfirmed};      // :53
inline constexpr EnumValue kTriangleList{4, Confidence::kConfirmed};   // :55
inline constexpr EnumValue kTriangleStrip{5, Confidence::kConfirmed};  // :57
inline constexpr EnumValue kTriangleFan{6, Confidence::kConfirmed};    // :59
}  // namespace operation_type

// VertexElementType. [confirmed] D3D9 declaration build 0x821CE5E0 switches on it with 12
// cases (@0x821CE6D4, table @0x821CE6F4) and produces the Xbox D3DDECLTYPE noted per value.
// SHORT1 and SHORT3 have no D3D9 equivalent and fall to the default (FLOAT3, r6 @0x821CE6B8),
// as upstream.
namespace vertex_element_type {
inline constexpr EnumValue kFloat1{0, Confidence::kConfirmed};      // :109 -> 0x002C83A4
inline constexpr EnumValue kFloat2{1, Confidence::kConfirmed};      // :110 -> 0x002C23A5
inline constexpr EnumValue kFloat3{2, Confidence::kConfirmed};      // :111 -> 0x002A23B9 (default)
inline constexpr EnumValue kFloat4{3, Confidence::kConfirmed};      // :112 -> 0x001A23A6
// :114. Also VertexDeclaration::addElement 0x82477058 replaces it with the render system's
// colour type.
inline constexpr EnumValue kColour{4, Confidence::kConfirmed};      // -> 0x00182886 (D3DCOLOR)
inline constexpr EnumValue kShort1{5, Confidence::kInferred};       // :115, default in the switch
inline constexpr EnumValue kShort2{6, Confidence::kConfirmed};      // :116 -> 0x002C2359
inline constexpr EnumValue kShort3{7, Confidence::kInferred};       // :117, default in the switch
inline constexpr EnumValue kShort4{8, Confidence::kConfirmed};      // :118 -> 0x001A235A
inline constexpr EnumValue kUbyte4{9, Confidence::kConfirmed};      // :119 -> 0x001A2286
// :121. addElement's fallback colour type when there is no render system (@0x82477094).
inline constexpr EnumValue kColourArgb{10, Confidence::kConfirmed};  // -> D3DCOLOR
inline constexpr EnumValue kColourAbgr{11, Confidence::kConfirmed};  // :123 -> D3DCOLOR
}  // namespace vertex_element_type

// VertexElementSemantic. [confirmed] same function switches on semantic-1 with 9 cases
// (@0x821CE780..@0x821CE7F8) mapping to D3DDECLUSAGE; usage index is 0 for DIFFUSE, 1 for
// SPECULAR and the element index otherwise (@0x821CE7FC..@0x821CE824).
namespace vertex_element_semantic {
inline constexpr EnumValue kPosition{1, Confidence::kConfirmed};            // :86  -> 0
inline constexpr EnumValue kBlendWeights{2, Confidence::kConfirmed};        // :88  -> 1
inline constexpr EnumValue kBlendIndices{3, Confidence::kConfirmed};        // :90  -> 2
inline constexpr EnumValue kNormal{4, Confidence::kConfirmed};              // :92  -> 3
inline constexpr EnumValue kDiffuse{5, Confidence::kConfirmed};             // :94  -> 10
inline constexpr EnumValue kSpecular{6, Confidence::kConfirmed};            // :96  -> 10
inline constexpr EnumValue kTextureCoordinates{7, Confidence::kConfirmed};  // :98  -> 5
inline constexpr EnumValue kBinormal{8, Confidence::kConfirmed};            // :100 -> 7
inline constexpr EnumValue kTangent{9, Confidence::kConfirmed};             // :102 -> 6
}  // namespace vertex_element_semantic

// HardwareIndexBuffer::IndexType. [confirmed] DefaultHardwareIndexBuffer ctor (inline in
// 0x82423560) picks the index size from it: < 1 -> 2 bytes, == 1 -> 4 bytes (@0x824235BC).
namespace index_type {
inline constexpr EnumValue k16Bit{0, Confidence::kConfirmed};  // OgreHardwareIndexBuffer.h:50
inline constexpr EnumValue k32Bit{1, Confidence::kConfirmed};  // :51
}  // namespace index_type

// HardwareBuffer::Usage bits (OgreHardwareBuffer.h:83..115).
namespace hardware_buffer_usage {
// [inferred] D3D9HardwareBufferManagerBase::createVertexBuffer 0x82580F68 rewrites
// STATIC_WRITE_ONLY (5) to 1 (@0x82580FB8); no test of the bit itself.
inline constexpr EnumValue kStatic{1, Confidence::kInferred};
// [confirmed] D3D9HardwareVertexBuffer::lockImpl tests it for HBL_NO_OVERWRITE (@0x821A7AB4).
inline constexpr EnumValue kDynamic{2, Confidence::kConfirmed};
// [confirmed] lockImpl tests it for HBL_READ_ONLY (@0x821A7A98).
inline constexpr EnumValue kWriteOnly{4, Confidence::kConfirmed};
// [confirmed] lockImpl @0x821A7AC0; createVertexBuffer @0x82580F94.
inline constexpr EnumValue kDiscardable{8, Confidence::kConfirmed};
// [confirmed] compared in createVertexBuffer @0x82580FB0 / @0x82580FA0.
inline constexpr EnumValue kStaticWriteOnly{5, Confidence::kConfirmed};
inline constexpr EnumValue kDynamicWriteOnly{6, Confidence::kConfirmed};
// [inferred] :115.
inline constexpr EnumValue kDynamicWriteOnlyDiscardable{14, Confidence::kInferred};
}  // namespace hardware_buffer_usage

// HardwareBuffer::LockOptions (OgreHardwareBuffer.h:123..136).
namespace lock_options {
inline constexpr EnumValue kNormal{0, Confidence::kInferred};
// [confirmed] lockImpl keeps an existing HBL_DISCARD (cmpwi 1 @0x821A7A3C).
inline constexpr EnumValue kDiscard{1, Confidence::kConfirmed};
// [confirmed] lockImpl skips the out-of-date marking for it (cmpwi 2 @0x821A7994); copyData
// 0x824231A0 locks the source with it (@0x824231C8).
inline constexpr EnumValue kReadOnly{2, Confidence::kConfirmed};
// [confirmed] lockImpl maps it to D3DLOCK_NOOVERWRITE 0x1000 (cmpwi 3 @0x821A7AAC).
inline constexpr EnumValue kNoOverwrite{3, Confidence::kConfirmed};
}  // namespace lock_options

// TextureType (OgreTexture.h:70..76).
namespace texture_type {
inline constexpr EnumValue k1D{1, Confidence::kInferred};
// [confirmed] Texture ctor default (@0x826557F0).
inline constexpr EnumValue k2D{2, Confidence::kConfirmed};
inline constexpr EnumValue k3D{3, Confidence::kInferred};
inline constexpr EnumValue kCubeMap{4, Confidence::kInferred};
}  // namespace texture_type

// TextureUsage bits (OgreTexture.h:52..61).
namespace texture_usage {
inline constexpr EnumValue kAutoMipmap{0x100, Confidence::kInferred};
// [confirmed] Runic Texture slot 72 impl 0x8257B6E0 tests bit 0x200 (@0x8257B6F4).
// D3D9Texture::_createSurfaceList 0x8257DFB0 also sets it in its pixel buffers' HardwareBuffer
// usage (ori @0x8257E00C, passed to the D3D9HardwarePixelBuffer ctor 0x82585F88 @0x8257E0AC), so
// a pixel buffer of a render target has it at hardware_buffer::kUsage.
inline constexpr EnumValue kRenderTarget{0x200, Confidence::kConfirmed};
// [confirmed] Texture ctor default 0x105 (@0x826557F8).
inline constexpr EnumValue kDefault{0x105, Confidence::kConfirmed};
}  // namespace texture_usage

// GpuProgramType (OgreGpuProgram.h:51..53). The bind paths branch on 0 / 1 / other
// (bindGpuProgramParameters @0x821C21B4), but nothing static ties 0 to vertex.
namespace gpu_program_type {
inline constexpr EnumValue kVertex{0, Confidence::kInferred};
inline constexpr EnumValue kFragment{1, Confidence::kInferred};
inline constexpr EnumValue kGeometry{2, Confidence::kInferred};
}  // namespace gpu_program_type

// GpuParamVariability bits (OgreGpuProgramParams.h:86..96).
namespace gpu_param_variability {
// [confirmed] bindGpuProgramParameters copies shared params when mask & 1 (@0x821C215C).
inline constexpr EnumValue kGlobal{1, Confidence::kConfirmed};
inline constexpr EnumValue kPerObject{2, Confidence::kInferred};
inline constexpr EnumValue kLights{4, Confidence::kInferred};
// [confirmed] bindGpuProgramParameters forwards mask == 8 to slot 92 (@0x821C2134).
inline constexpr EnumValue kPassIterationNumber{8, Confidence::kConfirmed};
inline constexpr EnumValue kAll{0xFFFF, Confidence::kInferred};
}  // namespace gpu_param_variability

// GpuProgramParameters::AutoConstantType (OgreGpuProgramParams.h:590, values implicit from 0).
// The guest's _updateAutoParams 0x82201EB0 switches on it through a 126-entry jump table at
// 0x82201F58 (@0x82201F38: cmplwi 0x7D), matching the 126 header values. Entries are
// [confirmed] only where the case body was checked against its upstream meaning. Four values
// fall to the default label in the guest although upstream handles them (Runic change in the
// function, not in the enum).
struct AutoConstantInfo {
  uint32_t value;
  const char* name;
  Confidence confidence;
  const char* evidence;
};

inline constexpr uint32_t kAutoConstantTypeCount = 126;

inline constexpr AutoConstantInfo kAutoConstantTypes[] = {
    {0, "ACT_WORLD_MATRIX", Confidence::kInferred, "OgreGpuProgramParams.h:593; header order; guest switch has a case at 0x82203384"},
    {1, "ACT_INVERSE_WORLD_MATRIX", Confidence::kInferred, "OgreGpuProgramParams.h:595; header order; guest switch has a case at 0x822033A0"},
    {2, "ACT_TRANSPOSE_WORLD_MATRIX", Confidence::kInferred, "OgreGpuProgramParams.h:599; header order; guest switch has a case at 0x822033BC"},
    {3, "ACT_INVERSE_TRANSPOSE_WORLD_MATRIX", Confidence::kInferred, "OgreGpuProgramParams.h:601; header order; guest switch has a case at 0x822033D0"},
    {4, "ACT_WORLD_MATRIX_ARRAY_3", Confidence::kInferred, "OgreGpuProgramParams.h:605; header order; guest switch has a case at 0x822033EC"},
    {5, "ACT_WORLD_MATRIX_ARRAY", Confidence::kInferred, "OgreGpuProgramParams.h:607; header order; guest switch has a case at 0x82203454"},
    {6, "ACT_VIEW_MATRIX", Confidence::kInferred, "OgreGpuProgramParams.h:610; header order; guest switch has a case at 0x82202150"},
    {7, "ACT_INVERSE_VIEW_MATRIX", Confidence::kInferred, "OgreGpuProgramParams.h:612; header order; guest switch has a case at 0x8220216C"},
    {8, "ACT_TRANSPOSE_VIEW_MATRIX", Confidence::kInferred, "OgreGpuProgramParams.h:616; header order; guest switch has a case at 0x82202188"},
    {9, "ACT_INVERSE_TRANSPOSE_VIEW_MATRIX", Confidence::kInferred, "OgreGpuProgramParams.h:620; header order; guest switch has a case at 0x8220219C"},
    {10, "ACT_PROJECTION_MATRIX", Confidence::kInferred, "OgreGpuProgramParams.h:624; header order; guest switch has a case at 0x822021B0"},
    {11, "ACT_INVERSE_PROJECTION_MATRIX", Confidence::kInferred, "OgreGpuProgramParams.h:628; header order; guest switch has a case at 0x822021CC"},
    {12, "ACT_TRANSPOSE_PROJECTION_MATRIX", Confidence::kInferred, "OgreGpuProgramParams.h:632; header order; guest switch has a case at 0x822021E0"},
    {13, "ACT_INVERSE_TRANSPOSE_PROJECTION_MATRIX", Confidence::kInferred, "OgreGpuProgramParams.h:636; header order; guest switch has a case at 0x822021F4"},
    {14, "ACT_VIEWPROJ_MATRIX", Confidence::kInferred, "OgreGpuProgramParams.h:640; header order; guest switch has a case at 0x82202208"},
    {15, "ACT_INVERSE_VIEWPROJ_MATRIX", Confidence::kInferred, "OgreGpuProgramParams.h:644; header order; guest switch has a case at 0x82202224"},
    {16, "ACT_TRANSPOSE_VIEWPROJ_MATRIX", Confidence::kInferred, "OgreGpuProgramParams.h:648; header order; guest switch has a case at 0x82202238"},
    {17, "ACT_INVERSE_TRANSPOSE_VIEWPROJ_MATRIX", Confidence::kInferred, "OgreGpuProgramParams.h:652; header order; guest switch has a case at 0x8220224C"},
    {18, "ACT_WORLDVIEW_MATRIX", Confidence::kInferred, "OgreGpuProgramParams.h:656; header order; guest switch has a case at 0x82203498"},
    {19, "ACT_INVERSE_WORLDVIEW_MATRIX", Confidence::kInferred, "OgreGpuProgramParams.h:658; header order; guest switch has a case at 0x822034B4"},
    {20, "ACT_TRANSPOSE_WORLDVIEW_MATRIX", Confidence::kInferred, "OgreGpuProgramParams.h:662; header order; guest switch has a case at 0x822034D0"},
    {21, "ACT_INVERSE_TRANSPOSE_WORLDVIEW_MATRIX", Confidence::kInferred, "OgreGpuProgramParams.h:664; header order; guest switch has a case at 0x822034E4"},
    {22, "ACT_WORLDVIEWPROJ_MATRIX", Confidence::kInferred, "OgreGpuProgramParams.h:669; header order; guest switch has a case at 0x82203500"},
    {23, "ACT_INVERSE_WORLDVIEWPROJ_MATRIX", Confidence::kInferred, "OgreGpuProgramParams.h:673; header order; guest switch has a case at 0x8220351C"},
    {24, "ACT_TRANSPOSE_WORLDVIEWPROJ_MATRIX", Confidence::kInferred, "OgreGpuProgramParams.h:677; header order; guest switch has a case at 0x82203530"},
    {25, "ACT_INVERSE_TRANSPOSE_WORLDVIEWPROJ_MATRIX", Confidence::kInferred, "OgreGpuProgramParams.h:681; header order; guest switch has a case at 0x82203544"},
    {26, "ACT_RENDER_TARGET_FLIPPING", Confidence::kConfirmed, "OgreGpuProgramParams.h:688; case 0x82202260: getCurrentRenderTarget()->requiresTextureFlipping() selects +1/-1"},
    {27, "ACT_VERTEX_WINDING", Confidence::kInferred, "OgreGpuProgramParams.h:692; header order; guest switch has a case at 0x822022BC"},
    {28, "ACT_FOG_COLOUR", Confidence::kInferred, "OgreGpuProgramParams.h:695; header order; guest switch has a case at 0x822023A4"},
    {29, "ACT_FOG_PARAMS", Confidence::kInferred, "OgreGpuProgramParams.h:697; header order; guest switch has a case at 0x822023D8"},
    {30, "ACT_SURFACE_AMBIENT_COLOUR", Confidence::kInferred, "OgreGpuProgramParams.h:701; header order; guest switch has a case at 0x82202408"},
    {31, "ACT_SURFACE_DIFFUSE_COLOUR", Confidence::kInferred, "OgreGpuProgramParams.h:703; header order; guest switch has a case at 0x82202438"},
    {32, "ACT_SURFACE_SPECULAR_COLOUR", Confidence::kInferred, "OgreGpuProgramParams.h:705; header order; guest switch has a case at 0x82202468"},
    {33, "ACT_SURFACE_EMISSIVE_COLOUR", Confidence::kInferred, "OgreGpuProgramParams.h:707; header order; guest switch has a case at 0x82202498"},
    {34, "ACT_SURFACE_SHININESS", Confidence::kInferred, "OgreGpuProgramParams.h:709; header order; guest switch has a case at 0x822024C8"},
    {35, "ACT_LIGHT_COUNT", Confidence::kInferred, "OgreGpuProgramParams.h:713; header order; guest switch has a case at 0x82203608"},
    {36, "ACT_AMBIENT_LIGHT_COLOUR", Confidence::kInferred, "OgreGpuProgramParams.h:717; header order; guest switch has a case at 0x8220230C"},
    {37, "ACT_LIGHT_DIFFUSE_COLOUR", Confidence::kInferred, "OgreGpuProgramParams.h:720; header order; guest switch has a case at 0x82203640"},
    {38, "ACT_LIGHT_SPECULAR_COLOUR", Confidence::kInferred, "OgreGpuProgramParams.h:722; header order; guest switch has a case at 0x82203674"},
    {39, "ACT_LIGHT_ATTENUATION", Confidence::kInferred, "OgreGpuProgramParams.h:724; header order; guest switch has a case at 0x8220394C"},
    {40, "ACT_SPOTLIGHT_PARAMS", Confidence::kInferred, "OgreGpuProgramParams.h:730; header order; guest switch has a case at 0x82203984"},
    {41, "ACT_LIGHT_POSITION", Confidence::kInferred, "OgreGpuProgramParams.h:732; header order; guest switch has a case at 0x822036A8"},
    {42, "ACT_LIGHT_POSITION_OBJECT_SPACE", Confidence::kInferred, "OgreGpuProgramParams.h:734; header order; guest switch has a case at 0x82202ED4"},
    {43, "ACT_LIGHT_POSITION_VIEW_SPACE", Confidence::kInferred, "OgreGpuProgramParams.h:736; header order; guest switch has a case at 0x82203730"},
    {44, "ACT_LIGHT_DIRECTION", Confidence::kInferred, "OgreGpuProgramParams.h:738; header order; guest switch has a case at 0x822036E0"},
    {45, "ACT_LIGHT_DIRECTION_OBJECT_SPACE", Confidence::kInferred, "OgreGpuProgramParams.h:740; header order; guest switch has a case at 0x82202F34"},
    {46, "ACT_LIGHT_DIRECTION_VIEW_SPACE", Confidence::kInferred, "OgreGpuProgramParams.h:742; header order; guest switch has a case at 0x82203790"},
    {47, "ACT_LIGHT_DISTANCE_OBJECT_SPACE", Confidence::kInferred, "OgreGpuProgramParams.h:747; header order; guest switch has a case at 0x82203010"},
    {48, "ACT_LIGHT_POWER_SCALE", Confidence::kInferred, "OgreGpuProgramParams.h:749; header order; guest switch has a case at 0x82203864"},
    {49, "ACT_LIGHT_DIFFUSE_COLOUR_POWER_SCALED", Confidence::kInferred, "OgreGpuProgramParams.h:751; header order; guest switch has a case at 0x822038A0"},
    {50, "ACT_LIGHT_SPECULAR_COLOUR_POWER_SCALED", Confidence::kInferred, "OgreGpuProgramParams.h:753; header order; guest switch has a case at 0x822038D8"},
    {51, "ACT_LIGHT_DIFFUSE_COLOUR_ARRAY", Confidence::kInferred, "OgreGpuProgramParams.h:755; header order; guest switch has a case at 0x822039BC"},
    {52, "ACT_LIGHT_SPECULAR_COLOUR_ARRAY", Confidence::kInferred, "OgreGpuProgramParams.h:757; header order; guest switch has a case at 0x82203A1C"},
    {53, "ACT_LIGHT_DIFFUSE_COLOUR_POWER_SCALED_ARRAY", Confidence::kInferred, "OgreGpuProgramParams.h:759; header order; guest switch has a case at 0x82203A7C"},
    {54, "ACT_LIGHT_SPECULAR_COLOUR_POWER_SCALED_ARRAY", Confidence::kInferred, "OgreGpuProgramParams.h:761; header order; guest switch has a case at 0x82203AE0"},
    {55, "ACT_LIGHT_ATTENUATION_ARRAY", Confidence::kInferred, "OgreGpuProgramParams.h:763; header order; guest switch has a case at 0x82203E0C"},
    {56, "ACT_LIGHT_POSITION_ARRAY", Confidence::kInferred, "OgreGpuProgramParams.h:765; header order; guest switch has a case at 0x82203B44"},
    {57, "ACT_LIGHT_POSITION_OBJECT_SPACE_ARRAY", Confidence::kInferred, "OgreGpuProgramParams.h:767; header order; guest switch has a case at 0x822030F0"},
    {58, "ACT_LIGHT_POSITION_VIEW_SPACE_ARRAY", Confidence::kInferred, "OgreGpuProgramParams.h:769; header order; guest switch has a case at 0x82203C20"},
    {59, "ACT_LIGHT_DIRECTION_ARRAY", Confidence::kInferred, "OgreGpuProgramParams.h:771; header order; guest switch has a case at 0x82203BA8"},
    {60, "ACT_LIGHT_DIRECTION_OBJECT_SPACE_ARRAY", Confidence::kInferred, "OgreGpuProgramParams.h:773; header order; guest switch has a case at 0x8220317C"},
    {61, "ACT_LIGHT_DIRECTION_VIEW_SPACE_ARRAY", Confidence::kInferred, "OgreGpuProgramParams.h:775; header order; guest switch has a case at 0x82203CAC"},
    {62, "ACT_LIGHT_DISTANCE_OBJECT_SPACE_ARRAY", Confidence::kInferred, "OgreGpuProgramParams.h:780; header order; guest switch has a case at 0x8220327C"},
    {63, "ACT_LIGHT_POWER_SCALE_ARRAY", Confidence::kInferred, "OgreGpuProgramParams.h:784; header order; guest switch has a case at 0x82203DA4"},
    {64, "ACT_SPOTLIGHT_PARAMS_ARRAY", Confidence::kInferred, "OgreGpuProgramParams.h:791; header order; guest switch has a case at 0x82203E70"},
    {65, "ACT_DERIVED_AMBIENT_LIGHT_COLOUR", Confidence::kInferred, "OgreGpuProgramParams.h:797; header order; guest switch has a case at 0x8220233C"},
    {66, "ACT_DERIVED_SCENE_COLOUR", Confidence::kInferred, "OgreGpuProgramParams.h:802; header order; guest switch has a case at 0x82202370"},
    {67, "ACT_DERIVED_LIGHT_DIFFUSE_COLOUR", Confidence::kInferred, "OgreGpuProgramParams.h:809; header order; guest switch has a case at 0x82203ED4"},
    {68, "ACT_DERIVED_LIGHT_SPECULAR_COLOUR", Confidence::kInferred, "OgreGpuProgramParams.h:815; header order; guest switch has a case at 0x82203F74"},
    {69, "ACT_DERIVED_LIGHT_DIFFUSE_COLOUR_ARRAY", Confidence::kInferred, "OgreGpuProgramParams.h:818; header order; guest switch has a case at 0x82204014"},
    {70, "ACT_DERIVED_LIGHT_SPECULAR_COLOUR_ARRAY", Confidence::kInferred, "OgreGpuProgramParams.h:820; header order; guest switch has a case at 0x822040DC"},
    {71, "ACT_LIGHT_NUMBER", Confidence::kInferred, "OgreGpuProgramParams.h:827; header order; guest switch has a case at 0x82203910"},
    {72, "ACT_LIGHT_CASTS_SHADOWS", Confidence::kInferred, "OgreGpuProgramParams.h:829; header only: the guest switch sends it to default (not handled; upstream handles it)"},
    {73, "ACT_SHADOW_EXTRUSION_DISTANCE", Confidence::kInferred, "OgreGpuProgramParams.h:835; header only: the guest switch sends it to default (not handled; upstream handles it)"},
    {74, "ACT_CAMERA_POSITION", Confidence::kInferred, "OgreGpuProgramParams.h:837; header order; guest switch has a case at 0x82202500"},
    {75, "ACT_CAMERA_POSITION_OBJECT_SPACE", Confidence::kInferred, "OgreGpuProgramParams.h:839; header order; guest switch has a case at 0x82203558"},
    {76, "ACT_TEXTURE_VIEWPROJ_MATRIX", Confidence::kInferred, "OgreGpuProgramParams.h:841; header order; guest switch has a case at 0x822041A4"},
    {77, "ACT_TEXTURE_VIEWPROJ_MATRIX_ARRAY", Confidence::kInferred, "OgreGpuProgramParams.h:843; header order; guest switch has a case at 0x822041B0"},
    {78, "ACT_TEXTURE_WORLDVIEWPROJ_MATRIX", Confidence::kInferred, "OgreGpuProgramParams.h:847; header order; guest switch has a case at 0x82202E5C"},
    {79, "ACT_TEXTURE_WORLDVIEWPROJ_MATRIX_ARRAY", Confidence::kInferred, "OgreGpuProgramParams.h:849; header order; guest switch has a case at 0x82202E68"},
    {80, "ACT_SPOTLIGHT_VIEWPROJ_MATRIX", Confidence::kInferred, "OgreGpuProgramParams.h:851; header order; guest switch has a case at 0x82204210"},
    {81, "ACT_SPOTLIGHT_WORLDVIEWPROJ_MATRIX", Confidence::kInferred, "OgreGpuProgramParams.h:855; header order; guest switch has a case at 0x82202EC8"},
    {82, "ACT_CUSTOM", Confidence::kConfirmed, "OgreGpuProgramParams.h:857; case 0x822035B8 shared with ACT_ANIMATION_PARAMETRIC (as in OgreGpuProgramParams.cpp): renderable->_updateCustomGpuParameter via vt+0x2C"},
    {83, "ACT_TIME", Confidence::kInferred, "OgreGpuProgramParams.h:860; header order; guest switch has a case at 0x82202530"},
    {84, "ACT_TIME_0_X", Confidence::kInferred, "OgreGpuProgramParams.h:864; header order; guest switch has a case at 0x82202570"},
    {85, "ACT_COSTIME_0_X", Confidence::kInferred, "OgreGpuProgramParams.h:866; header order; guest switch has a case at 0x822025AC"},
    {86, "ACT_SINTIME_0_X", Confidence::kInferred, "OgreGpuProgramParams.h:868; header order; guest switch has a case at 0x822025E8"},
    {87, "ACT_TANTIME_0_X", Confidence::kInferred, "OgreGpuProgramParams.h:870; header order; guest switch has a case at 0x82202624"},
    {88, "ACT_TIME_0_X_PACKED", Confidence::kInferred, "OgreGpuProgramParams.h:874; header order; guest switch has a case at 0x82202660"},
    {89, "ACT_TIME_0_1", Confidence::kInferred, "OgreGpuProgramParams.h:879; header order; guest switch has a case at 0x82202698"},
    {90, "ACT_COSTIME_0_1", Confidence::kInferred, "OgreGpuProgramParams.h:881; header order; guest switch has a case at 0x822026D4"},
    {91, "ACT_SINTIME_0_1", Confidence::kInferred, "OgreGpuProgramParams.h:883; header order; guest switch has a case at 0x82202710"},
    {92, "ACT_TANTIME_0_1", Confidence::kInferred, "OgreGpuProgramParams.h:885; header order; guest switch has a case at 0x8220274C"},
    {93, "ACT_TIME_0_1_PACKED", Confidence::kInferred, "OgreGpuProgramParams.h:889; header order; guest switch has a case at 0x82202788"},
    {94, "ACT_TIME_0_2PI", Confidence::kInferred, "OgreGpuProgramParams.h:894; header order; guest switch has a case at 0x822027C0"},
    {95, "ACT_COSTIME_0_2PI", Confidence::kInferred, "OgreGpuProgramParams.h:896; header order; guest switch has a case at 0x822027FC"},
    {96, "ACT_SINTIME_0_2PI", Confidence::kInferred, "OgreGpuProgramParams.h:898; header order; guest switch has a case at 0x82202838"},
    {97, "ACT_TANTIME_0_2PI", Confidence::kInferred, "OgreGpuProgramParams.h:900; header order; guest switch has a case at 0x82202874"},
    {98, "ACT_TIME_0_2PI_PACKED", Confidence::kInferred, "OgreGpuProgramParams.h:904; header order; guest switch has a case at 0x822028B0"},
    {99, "ACT_FRAME_TIME", Confidence::kInferred, "OgreGpuProgramParams.h:906; header order; guest switch has a case at 0x822028E8"},
    {100, "ACT_FPS", Confidence::kInferred, "OgreGpuProgramParams.h:908; header order; guest switch has a case at 0x82202928"},
    {101, "ACT_VIEWPORT_WIDTH", Confidence::kInferred, "OgreGpuProgramParams.h:913; header order; guest switch has a case at 0x82202960"},
    {102, "ACT_VIEWPORT_HEIGHT", Confidence::kInferred, "OgreGpuProgramParams.h:917; header order; guest switch has a case at 0x82202998"},
    {103, "ACT_INVERSE_VIEWPORT_WIDTH", Confidence::kInferred, "OgreGpuProgramParams.h:921; header order; guest switch has a case at 0x822029D0"},
    {104, "ACT_INVERSE_VIEWPORT_HEIGHT", Confidence::kInferred, "OgreGpuProgramParams.h:925; header order; guest switch has a case at 0x82202A08"},
    {105, "ACT_VIEWPORT_SIZE", Confidence::kConfirmed, "OgreGpuProgramParams.h:929; case 0x82202A40 reads four consecutive float getters (width, height, 1/width, 1/height)"},
    {106, "ACT_VIEW_DIRECTION", Confidence::kInferred, "OgreGpuProgramParams.h:935; header order; guest switch has a case at 0x82202C58"},
    {107, "ACT_VIEW_SIDE_VECTOR", Confidence::kInferred, "OgreGpuProgramParams.h:939; header order; guest switch has a case at 0x82202C90"},
    {108, "ACT_VIEW_UP_VECTOR", Confidence::kInferred, "OgreGpuProgramParams.h:943; header order; guest switch has a case at 0x82202CC8"},
    {109, "ACT_FOV", Confidence::kInferred, "OgreGpuProgramParams.h:947; header order; guest switch has a case at 0x82202D00"},
    {110, "ACT_NEAR_CLIP_DISTANCE", Confidence::kInferred, "OgreGpuProgramParams.h:951; header order; guest switch has a case at 0x82202D38"},
    {111, "ACT_FAR_CLIP_DISTANCE", Confidence::kInferred, "OgreGpuProgramParams.h:955; header order; guest switch has a case at 0x82202D70"},
    {112, "ACT_PASS_NUMBER", Confidence::kInferred, "OgreGpuProgramParams.h:960; header order; guest switch has a case at 0x82202DA8"},
    {113, "ACT_PASS_ITERATION_NUMBER", Confidence::kConfirmed, "OgreGpuProgramParams.h:966; case 0x82202DF4 writes 0.0f and stores physicalIndex into mActivePassIterationIndex (+0x64)"},
    {114, "ACT_ANIMATION_PARAMETRIC", Confidence::kConfirmed, "OgreGpuProgramParams.h:972; case 0x822035B8 shared with ACT_CUSTOM"},
    {115, "ACT_TEXEL_OFFSETS", Confidence::kInferred, "OgreGpuProgramParams.h:979; header order; guest switch has a case at 0x82202AC4"},
    {116, "ACT_SCENE_DEPTH_RANGE", Confidence::kInferred, "OgreGpuProgramParams.h:985; header order; guest switch has a case at 0x82202C28"},
    {117, "ACT_SHADOW_SCENE_DEPTH_RANGE", Confidence::kInferred, "OgreGpuProgramParams.h:992; header only: the guest switch sends it to default (not handled; upstream handles it)"},
    {118, "ACT_SHADOW_COLOUR", Confidence::kInferred, "OgreGpuProgramParams.h:997; header only: the guest switch sends it to default (not handled; upstream handles it)"},
    {119, "ACT_TEXTURE_SIZE", Confidence::kInferred, "OgreGpuProgramParams.h:1001; header order; guest switch has a case at 0x82202B80"},
    {120, "ACT_INVERSE_TEXTURE_SIZE", Confidence::kInferred, "OgreGpuProgramParams.h:1005; header order; guest switch has a case at 0x82202BB8"},
    {121, "ACT_PACKED_TEXTURE_SIZE", Confidence::kInferred, "OgreGpuProgramParams.h:1009; header order; guest switch has a case at 0x82202BF0"},
    {122, "ACT_TEXTURE_MATRIX", Confidence::kInferred, "OgreGpuProgramParams.h:1014; header order; guest switch has a case at 0x82202E20"},
    {123, "ACT_LOD_CAMERA_POSITION", Confidence::kInferred, "OgreGpuProgramParams.h:1021; header order; guest switch has a case at 0x82202E2C"},
    {124, "ACT_LOD_CAMERA_POSITION_OBJECT_SPACE", Confidence::kInferred, "OgreGpuProgramParams.h:1027; header order; guest switch has a case at 0x82203588"},
    {125, "ACT_LIGHT_CUSTOM", Confidence::kInferred, "OgreGpuProgramParams.h:1029; header order; guest switch has a case at 0x822035E8"},
};

namespace auto_constant_type {
inline constexpr EnumValue kWorldMatrix{0, Confidence::kInferred};
inline constexpr EnumValue kRenderTargetFlipping{26, Confidence::kConfirmed};
inline constexpr EnumValue kCustom{82, Confidence::kConfirmed};
inline constexpr EnumValue kViewportSize{105, Confidence::kConfirmed};
inline constexpr EnumValue kPassIterationNumber{113, Confidence::kConfirmed};
inline constexpr EnumValue kAnimationParametric{114, Confidence::kConfirmed};
inline constexpr EnumValue kLightCustom{125, Confidence::kInferred};
}  // namespace auto_constant_type


// SceneBlendFactor (OgreBlendMode.h:236..245). All [confirmed] by D3D9Mappings::get 0x82201750
// (called by _setSceneBlending 0x821C52B0 @0x821C5380): guest 0..9 -> Xbox D3DBLEND
// 1, 0, 8, 4, 9, 5, 10, 6, 11, 7; _setSceneBlending disables blending for (ONE, ZERO) @0x821C52C8.
namespace scene_blend_factor {
inline constexpr EnumValue kOne{0, Confidence::kConfirmed};
inline constexpr EnumValue kZero{1, Confidence::kConfirmed};
inline constexpr EnumValue kDestColour{2, Confidence::kConfirmed};
inline constexpr EnumValue kSourceColour{3, Confidence::kConfirmed};
inline constexpr EnumValue kOneMinusDestColour{4, Confidence::kConfirmed};
inline constexpr EnumValue kOneMinusSourceColour{5, Confidence::kConfirmed};
inline constexpr EnumValue kDestAlpha{6, Confidence::kConfirmed};
inline constexpr EnumValue kSourceAlpha{7, Confidence::kConfirmed};
inline constexpr EnumValue kOneMinusDestAlpha{8, Confidence::kConfirmed};
inline constexpr EnumValue kOneMinusSourceAlpha{9, Confidence::kConfirmed};
}  // namespace scene_blend_factor

// SceneBlendOperation (OgreBlendMode.h:255..259). [confirmed] D3D9Mappings::get 0x821BF8B0:
// guest 0..4 -> D3DBLENDOP 0 (add), 1 (subtract), 4 (revsubtract), 2 (min), 3 (max).
namespace scene_blend_operation {
inline constexpr EnumValue kAdd{0, Confidence::kConfirmed};
inline constexpr EnumValue kSubtract{1, Confidence::kConfirmed};
inline constexpr EnumValue kReverseSubtract{2, Confidence::kConfirmed};
inline constexpr EnumValue kMin{3, Confidence::kConfirmed};
inline constexpr EnumValue kMax{4, Confidence::kConfirmed};
}  // namespace scene_blend_operation

// CompareFunction (OgreCommon.h:67..74). [confirmed] D3D9Mappings::get 0x821C2FA0: guest 0..7 ->
// D3DCMP 0 (never), 7 (always), 1 (less), 3 (lessequal), 2 (equal), 5 (notequal),
// 6 (greaterequal), 4 (greater). _setAlphaRejectSettings 0x821C54D8 disables alpha test for
// ALWAYS_PASS (@0x821C54F8).
namespace compare_function {
inline constexpr EnumValue kAlwaysFail{0, Confidence::kConfirmed};
inline constexpr EnumValue kAlwaysPass{1, Confidence::kConfirmed};
inline constexpr EnumValue kLess{2, Confidence::kConfirmed};
inline constexpr EnumValue kLessEqual{3, Confidence::kConfirmed};
inline constexpr EnumValue kEqual{4, Confidence::kConfirmed};
inline constexpr EnumValue kNotEqual{5, Confidence::kConfirmed};
inline constexpr EnumValue kGreaterEqual{6, Confidence::kConfirmed};
inline constexpr EnumValue kGreater{7, Confidence::kConfirmed};
}  // namespace compare_function

// [runic] D3D9RenderSystem::_setDepthBufferFunction 0x821C3010 swaps LESS<->GREATER and
// LESS_EQUAL<->GREATER_EQUAL (@0x821C301C..@0x821C305C) before mapping: the guest renders with
// inverted depth. Only the depth function is swapped; alpha reject and stencil are not.
inline constexpr uint32_t RunicEffectiveDepthFunction(uint32_t requested) {
  switch (requested) {
    case 2: return 7;
    case 3: return 6;
    case 6: return 3;
    case 7: return 2;
    default: return requested;
  }
}
inline constexpr Confidence kRunicDepthFunctionInversion = Confidence::kRunicDifference;

// [runic] The rest of the guest's inverted depth, consistent with the function swap above:
// - _setViewport 0x821A4368 sets the D3D viewport depth range to MinZ = 1.0, MaxZ = 0.0
//   (constants 0x820AA300 / 0x820AA304 loaded @0x821A4448 / @0x821A444C);
// - clearFrameBuffer 0x8219CAF8 clears depth to 1.0 - requested (fsubs @0x8219CBBC, 1.0 at
//   0x820AA300).
// The projection OGRE passes is standard D3D (near -> 0, far -> 1). Hooks record the values OGRE
// requested (before these inversions), so standard depth is obtained by not reproducing them.
inline constexpr float kRunicViewportMinZ = 1.0f;
inline constexpr float kRunicViewportMaxZ = 0.0f;
inline constexpr Confidence kRunicInvertedDepthRange = Confidence::kRunicDifference;

// CullingMode (OgreCommon.h:139..143). [confirmed] _setCullingMode 0x821C6CF8: 1 -> D3DCULL none,
// 2 -> CW, 3 -> CCW, with CW/CCW swapped when (active target flips) != invertVertexWinding
// (@0x821C6D2C..@0x821C6DC0).
namespace culling_mode {
inline constexpr EnumValue kNone{1, Confidence::kConfirmed};
inline constexpr EnumValue kClockwise{2, Confidence::kConfirmed};
inline constexpr EnumValue kAnticlockwise{3, Confidence::kConfirmed};
}  // namespace culling_mode

// FilterType (OgreCommon.h:94..98). [confirmed] _setTextureUnitFiltering 0x821CA470 picks
// MINFILTER/MAGFILTER/MIPFILTER for 0/1/2 (@0x821CA49C..@0x821CA4CC).
namespace filter_type {
inline constexpr EnumValue kMin{0, Confidence::kConfirmed};
inline constexpr EnumValue kMag{1, Confidence::kConfirmed};
inline constexpr EnumValue kMip{2, Confidence::kConfirmed};
}  // namespace filter_type

// FilterOptions (OgreCommon.h:104..110). [confirmed] D3D9Mappings::get 0x821CA528: 0 -> D3DTEXF
// none (2), 1 -> point (0), 2 -> linear (1), 3 -> anisotropic (4) when the caps allow it.
namespace filter_options {
inline constexpr EnumValue kNone{0, Confidence::kConfirmed};
inline constexpr EnumValue kPoint{1, Confidence::kConfirmed};
inline constexpr EnumValue kLinear{2, Confidence::kConfirmed};
inline constexpr EnumValue kAnisotropic{3, Confidence::kConfirmed};
}  // namespace filter_options

// TextureUnitState::TextureAddressingMode (OgreTextureUnitState.h:128..134). [confirmed]
// _setTextureAddressingMode 0x821CA1A8: 0 -> wrap, 1 -> mirror, 2 -> clamp, 3 -> border (caps)
// (@0x821CA1CC..@0x821CA210).
namespace texture_addressing_mode {
inline constexpr EnumValue kWrap{0, Confidence::kConfirmed};
inline constexpr EnumValue kMirror{1, Confidence::kConfirmed};
inline constexpr EnumValue kClamp{2, Confidence::kConfirmed};
inline constexpr EnumValue kBorder{3, Confidence::kConfirmed};
}  // namespace texture_addressing_mode

// FrameBufferType bits (OgreCommon.h:297..299). [confirmed] clearFrameBuffer 0x8219CAF8: bit 1 ->
// D3DCLEAR target 0xF, bit 2 -> 0x10 (zbuffer), bit 4 -> 0x20 (stencil, if the caps allow it).
namespace frame_buffer_type {
inline constexpr EnumValue kColour{0x1, Confidence::kConfirmed};
inline constexpr EnumValue kDepth{0x2, Confidence::kConfirmed};
inline constexpr EnumValue kStencil{0x4, Confidence::kConfirmed};
}  // namespace frame_buffer_type

// PolygonMode (OgreCommon.h:183..187). _setPolygonMode 0x821C5228: 1 -> D3DFILL point (1),
// 2 -> wireframe (0x25) [confirmed]; 3 takes the remaining branch [inferred solid].
namespace polygon_mode {
inline constexpr EnumValue kPoints{1, Confidence::kConfirmed};
inline constexpr EnumValue kWireframe{2, Confidence::kConfirmed};
inline constexpr EnumValue kSolid{3, Confidence::kInferred};
}  // namespace polygon_mode

// StencilOperation (OgreRenderSystem.h:80..94). [confirmed] D3D9Mappings::get 0x825842B8 (with
// the invert flag): 0 -> keep, 1 -> zero, 2 -> replace, 3/4 -> incrsat/decrsat, 5/6 ->
// incr/decr (swapped when inverted), 7 -> invert.
namespace stencil_operation {
inline constexpr EnumValue kKeep{0, Confidence::kConfirmed};
inline constexpr EnumValue kZero{1, Confidence::kConfirmed};
inline constexpr EnumValue kReplace{2, Confidence::kConfirmed};
inline constexpr EnumValue kIncrement{3, Confidence::kConfirmed};
inline constexpr EnumValue kDecrement{4, Confidence::kConfirmed};
inline constexpr EnumValue kIncrementWrap{5, Confidence::kConfirmed};
inline constexpr EnumValue kDecrementWrap{6, Confidence::kConfirmed};
inline constexpr EnumValue kInvert{7, Confidence::kConfirmed};
}  // namespace stencil_operation


// LayerBlendType (OgreBlendMode.h:46..47). [confirmed] _setTextureBlendMode branches on 0/1.
namespace layer_blend_type {
inline constexpr EnumValue kColour{0, Confidence::kConfirmed};
inline constexpr EnumValue kAlpha{1, Confidence::kConfirmed};
}  // namespace layer_blend_type

// LayerBlendOperationEx (OgreBlendMode.h:77..105). [inferred] header order; only kModulate is
// used, to mark draws whose texture combination is not plain modulate.
namespace layer_blend_operation_ex {
inline constexpr EnumValue kSource1{0, Confidence::kInferred};
inline constexpr EnumValue kSource2{1, Confidence::kInferred};
inline constexpr EnumValue kModulate{2, Confidence::kInferred};
}  // namespace layer_blend_operation_ex

// LayerBlendSource (OgreBlendMode.h:115..123). [confirmed] kManual: _setTextureBlendMode compares
// source1/source2 with 4 (@0x821C9238, @0x821C9334); the rest [inferred] header order.
namespace layer_blend_source {
inline constexpr EnumValue kCurrent{0, Confidence::kInferred};
inline constexpr EnumValue kTexture{1, Confidence::kInferred};
inline constexpr EnumValue kDiffuse{2, Confidence::kInferred};
inline constexpr EnumValue kSpecular{3, Confidence::kInferred};
inline constexpr EnumValue kManual{4, Confidence::kConfirmed};
}  // namespace layer_blend_source

}  // namespace torchlight::guest_abi::ogre
