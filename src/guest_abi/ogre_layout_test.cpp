// Compiles the guest ABI headers and checks their internal consistency and the big-endian
// readers. It does not (and cannot) check the offsets against the guest: that evidence lives
// in the comments.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <string_view>

#include "guest_abi/game_ui.h"
#include "guest_abi/guest_functions.h"
#include "guest_abi/ogre_enums.h"
#include "guest_abi/ogre_layout.h"
#include "guest_abi/xbox_d3d.h"

namespace {

using namespace torchlight::guest_abi;
using namespace torchlight::guest_abi::ogre;

template <size_t N>
constexpr bool IndicesAreDense(const SlotInfo (&table)[N]) {
  for (size_t i = 0; i < N; ++i) {
    if (table[i].index != i || table[i].name == nullptr || table[i].evidence == nullptr) {
      return false;
    }
  }
  return true;
}

template <size_t N>
constexpr bool SlotIs(const SlotInfo (&table)[N], VtableSlot slot, std::string_view name) {
  return slot.index < N && std::string_view(table[slot.index].name) == name &&
         table[slot.index].confidence == slot.confidence;
}

template <size_t N>
constexpr bool RunicNamesMatchConfidence(const SlotInfo (&table)[N]) {
  for (const SlotInfo& s : table) {
    bool runic_name = std::string_view(s.name).starts_with("kRunic");
    if (runic_name != (s.confidence == Confidence::kRunicDifference)) return false;
  }
  return true;
}

constexpr bool Increasing(std::initializer_list<Field> fields, uint32_t size) {
  uint32_t prev = 0;
  bool first = true;
  for (Field f : fields) {
    if (!first && f.offset <= prev) return false;
    if (f.offset >= size) return false;
    prev = f.offset;
    first = false;
  }
  return true;
}

// RenderSystem vtable.
static_assert(std::size(kRenderSystemSlots) == render_system_vtable::kSlotCount);
static_assert(IndicesAreDense(kRenderSystemSlots));
static_assert(RunicNamesMatchConfidence(kRenderSystemSlots));
static_assert(SlotIs(kRenderSystemSlots, render_system_vtable::kRender, "_render"));
static_assert(SlotIs(kRenderSystemSlots, render_system_vtable::kSetTexture, "_setTexture"));
static_assert(SlotIs(kRenderSystemSlots, render_system_vtable::kSetTextureByName, "_setTexture"));
static_assert(SlotIs(kRenderSystemSlots, render_system_vtable::kBeginFrame, "_beginFrame"));
static_assert(SlotIs(kRenderSystemSlots, render_system_vtable::kEndFrame, "_endFrame"));
static_assert(SlotIs(kRenderSystemSlots, render_system_vtable::kSetViewport, "_setViewport"));
static_assert(SlotIs(kRenderSystemSlots, render_system_vtable::kSetVertexDeclaration,
                     "setVertexDeclaration"));
static_assert(SlotIs(kRenderSystemSlots, render_system_vtable::kSetVertexBufferBinding,
                     "setVertexBufferBinding"));
static_assert(SlotIs(kRenderSystemSlots, render_system_vtable::kRunicSlot89_UnknownStringGetter,
                     "kRunicSlot89_UnknownStringGetter"));
static_assert(SlotIs(kRenderSystemSlots, render_system_vtable::kBindGpuProgram, "bindGpuProgram"));
static_assert(SlotIs(kRenderSystemSlots, render_system_vtable::kBindGpuProgramParameters,
                     "bindGpuProgramParameters"));
static_assert(SlotIs(kRenderSystemSlots,
                     render_system_vtable::kBindGpuProgramPassIterationParameters,
                     "bindGpuProgramPassIterationParameters"));
static_assert(SlotIs(kRenderSystemSlots, render_system_vtable::kUnbindGpuProgram,
                     "unbindGpuProgram"));
static_assert(SlotIs(kRenderSystemSlots, render_system_vtable::kClearFrameBuffer,
                     "clearFrameBuffer"));
static_assert(SlotIs(kRenderSystemSlots, render_system_vtable::kSetRenderTarget,
                     "_setRenderTarget"));
static_assert(SlotIs(kRenderSystemSlots, render_system_vtable::kSetClipPlanesImpl,
                     "setClipPlanesImpl"));
static_assert(render_system_vtable::kRender.byte_offset() == 0x15C);
static_assert(kRenderSystemSlots[87].guest_impl == 0x821C4058);

// Texture vtable.
static_assert(std::size(kTextureSlots) == texture_vtable::kSlotCount);
static_assert(IndicesAreDense(kTextureSlots));
static_assert(RunicNamesMatchConfidence(kTextureSlots));
static_assert(SlotIs(kTextureSlots, texture_vtable::kLoad, "load"));
static_assert(SlotIs(kTextureSlots, texture_vtable::kTouch, "touch"));
static_assert(SlotIs(kTextureSlots, texture_vtable::kGetName, "getName"));
static_assert(SlotIs(kTextureSlots, texture_vtable::kGetTextureType, "getTextureType"));
static_assert(SlotIs(kTextureSlots, texture_vtable::kGetNumMipmaps, "getNumMipmaps"));
static_assert(SlotIs(kTextureSlots, texture_vtable::kGetHeight, "getHeight"));
static_assert(SlotIs(kTextureSlots, texture_vtable::kGetWidth, "getWidth"));
static_assert(SlotIs(kTextureSlots, texture_vtable::kGetDepth, "getDepth"));
static_assert(SlotIs(kTextureSlots, texture_vtable::kGetUsage, "getUsage"));
static_assert(SlotIs(kTextureSlots, texture_vtable::kCreateInternalResources,
                     "createInternalResources"));
static_assert(SlotIs(kTextureSlots, texture_vtable::kFreeInternalResources,
                     "freeInternalResources"));
static_assert(SlotIs(kTextureSlots, texture_vtable::kRunicSlot72_ReleaseSurfaces,
                     "kRunicSlot72_ReleaseSurfaces"));
static_assert(SlotIs(kTextureSlots, texture_vtable::kGetFormat, "getFormat"));
static_assert(SlotIs(kTextureSlots, texture_vtable::kGetBuffer, "getBuffer"));
// The Texture vtable's getters read the fields the layout names.
static_assert(texture_vtable::kGetTextureType.byte_offset() == 0xB0);  // _setTexture vt+0xB0

// Small vtables: named slots are distinct and inside the table.
static_assert(hardware_buffer_vtable::kUpdateFromShadow.index + 1 ==
              hardware_buffer_vtable::kSlotCount);
static_assert(vertex_declaration_vtable::kClone.index + 1 == vertex_declaration_vtable::kSlotCount);
static_assert(vertex_buffer_binding_vtable::kCloseGaps.index + 1 ==
              vertex_buffer_binding_vtable::kSlotCount);
static_assert(hardware_buffer_manager_vtable::kCreateVertexBuffer.byte_offset() == 0x28);
static_assert(hardware_buffer_manager_vtable::kCreateIndexBuffer.index ==
              hardware_buffer_manager_vtable::kCreateVertexBuffer.index + 1);

// Layout coherence: members increase and fit in the class size.
static_assert(Increasing({shared_ptr::kVptr, shared_ptr::kPRep, shared_ptr::kPUseCount,
                          shared_ptr::kUseFreeMethod},
                         shared_ptr::kSize.bytes));
static_assert(Increasing({stl_vector::kFirst, stl_vector::kLast, stl_vector::kEnd,
                          stl_vector::kAllocator},
                         stl_vector::kSize.bytes));
static_assert(Increasing({render_operation::kVertexData, render_operation::kOperationType,
                          render_operation::kUseIndexes, render_operation::kIndexData,
                          render_operation::kSrcRenderable,
                          render_operation::kRunicInstanceCount},
                         0x18));
static_assert(Increasing({vertex_data::kMgr, vertex_data::kVertexDeclaration,
                          vertex_data::kVertexBufferBinding, vertex_data::kDeleteDclBinding,
                          vertex_data::kVertexStart, vertex_data::kVertexCount},
                         0x18));
static_assert(index_data::kIndexStart.offset ==
              index_data::kIndexBuffer.offset + shared_ptr::kSize.bytes);
static_assert(Increasing({vertex_element::kSource, vertex_element::kOffset, vertex_element::kType,
                          vertex_element::kSemantic, vertex_element::kIndex},
                         vertex_element::kSize.bytes));
static_assert(vertex_declaration::kElementList.offset + stl_vector::kSize.bytes ==
              vertex_declaration::kSize.bytes);
static_assert(vertex_buffer_binding::kBindingMap.offset + stl_tree::kSizeof.bytes ==
              vertex_buffer_binding::kHighIndex.offset);
static_assert(Increasing({hardware_buffer::kSizeInBytes, hardware_buffer::kUsage,
                          hardware_buffer::kIsLocked, hardware_buffer::kLockStart,
                          hardware_buffer::kLockSize, hardware_buffer::kSystemMemory,
                          hardware_buffer::kUseShadowBuffer},
                         hardware_buffer::kSize.bytes));
static_assert(hardware_vertex_buffer::kMgr.offset == hardware_buffer::kSize.bytes);
static_assert(hardware_index_buffer::kMgr.offset == hardware_buffer::kSize.bytes);
static_assert(Increasing({hardware_vertex_buffer::kMgr, hardware_vertex_buffer::kNumVertices,
                          hardware_vertex_buffer::kVertexSize,
                          hardware_vertex_buffer::kRunicIsInstanceData,
                          hardware_vertex_buffer::kRunicInstanceDataStepRate},
                         hardware_vertex_buffer::kSize.bytes));
static_assert(Increasing({hardware_index_buffer::kMgr, hardware_index_buffer::kIndexType,
                          hardware_index_buffer::kNumIndexes, hardware_index_buffer::kIndexSize},
                         hardware_index_buffer::kSize.bytes));
static_assert(default_hardware_vertex_buffer::kData.offset == hardware_vertex_buffer::kSize.bytes);
static_assert(default_hardware_index_buffer::kData.offset == hardware_index_buffer::kSize.bytes);
// D3D9 buffers: D3D9Resource (vptr only) follows the OGRE base, then the device map.
static_assert(d3d9_hardware_vertex_buffer::kDeviceToResourcesMap.offset ==
              hardware_vertex_buffer::kSize.bytes + 4);
static_assert(d3d9_hardware_index_buffer::kDeviceToResourcesMap.offset ==
              hardware_index_buffer::kSize.bytes + 4);
static_assert(d3d9_hardware_vertex_buffer::kSystemMemoryBuffer.offset + 4 ==
              d3d9_hardware_vertex_buffer::kSize.bytes);
static_assert(d3d9_hardware_index_buffer::kSystemMemoryBuffer.offset + 4 ==
              d3d9_hardware_index_buffer::kSize.bytes);
static_assert(d3d9_vertex_declaration::kDeviceToDeclarationMap.offset ==
              vertex_declaration::kSize.bytes + 4);
static_assert(texture::kHeight.offset == resource::kSize.bytes);
static_assert(Increasing({texture::kHeight, texture::kWidth, texture::kDepth,
                          texture::kNumRequestedMipmaps, texture::kNumMipmaps,
                          texture::kMipmapsHardwareGenerated, texture::kGamma, texture::kHwGamma,
                          texture::kFSAA, texture::kFSAAHint, texture::kTextureType,
                          texture::kFormat, texture::kUsage, texture::kSrcFormat,
                          texture::kSrcWidth, texture::kSrcHeight, texture::kSrcDepth,
                          texture::kDesiredFormat, texture::kDesiredIntegerBitDepth,
                          texture::kDesiredFloatBitDepth, texture::kTreatLuminanceAsAlpha,
                          texture::kInternalResourcesCreated},
                         texture::kSize.bytes));
static_assert(texture::kTextureType.offset == texture::kFSAAHint.offset + stl_string::kSize.bytes);
static_assert(d3d9_texture::kDeviceToTextureResourcesMap.offset == texture::kSize.bytes + 4);
// GpuProgram: mType, mFilename (String), mSource (String).
static_assert(gpu_program::kSource.offset ==
              gpu_program::kType.offset + 4 + stl_string::kSize.bytes);
// D3D9HLSLProgram: mTarget, mEntryPoint, mPreprocessorDefines are consecutive Strings.
static_assert(d3d9_hlsl_program::kEntryPoint.offset ==
              d3d9_hlsl_program::kTarget.offset + stl_string::kSize.bytes);
static_assert(d3d9_hlsl_program::kPreprocessorDefines.offset ==
              d3d9_hlsl_program::kEntryPoint.offset + stl_string::kSize.bytes);
static_assert(Increasing({gpu_program_parameters::kFloatConstants,
                          gpu_program_parameters::kIntConstants,
                          gpu_program_parameters::kFloatLogicalToPhysical,
                          gpu_program_parameters::kIntLogicalToPhysical,
                          gpu_program_parameters::kNamedConstants,
                          gpu_program_parameters::kAutoConstants,
                          gpu_program_parameters::kCombinedVariability,
                          gpu_program_parameters::kTransposeMatrices,
                          gpu_program_parameters::kIgnoreMissingParams,
                          gpu_program_parameters::kActivePassIterationIndex,
                          gpu_program_parameters::kSharedParamSets,
                          gpu_program_parameters::kRenderSystemData},
                         0x80));
static_assert(Increasing({auto_constant_entry::kParamType, auto_constant_entry::kPhysicalIndex,
                          auto_constant_entry::kElementCount, auto_constant_entry::kData,
                          auto_constant_entry::kVariability},
                         auto_constant_entry::kSize.bytes));

// Enums.
static_assert(std::size(kAutoConstantTypes) == kAutoConstantTypeCount);
constexpr bool AutoConstantTypesDense() {
  for (size_t i = 0; i < std::size(kAutoConstantTypes); ++i) {
    if (kAutoConstantTypes[i].value != i) return false;
  }
  return true;
}
static_assert(AutoConstantTypesDense());
constexpr bool ActIs(EnumValue v, std::string_view name) {
  return std::string_view(kAutoConstantTypes[v.value].name) == name &&
         kAutoConstantTypes[v.value].confidence == v.confidence;
}
static_assert(ActIs(auto_constant_type::kWorldMatrix, "ACT_WORLD_MATRIX"));
static_assert(ActIs(auto_constant_type::kRenderTargetFlipping, "ACT_RENDER_TARGET_FLIPPING"));
static_assert(ActIs(auto_constant_type::kCustom, "ACT_CUSTOM"));
static_assert(ActIs(auto_constant_type::kViewportSize, "ACT_VIEWPORT_SIZE"));
static_assert(ActIs(auto_constant_type::kPassIterationNumber, "ACT_PASS_ITERATION_NUMBER"));
static_assert(ActIs(auto_constant_type::kAnimationParametric, "ACT_ANIMATION_PARAMETRIC"));
static_assert(ActIs(auto_constant_type::kLightCustom, "ACT_LIGHT_CUSTOM"));
static_assert(operation_type::kTriangleFan.value == 6);
static_assert(texture_usage::kDefault.value ==
              (texture_usage::kAutoMipmap.value | hardware_buffer_usage::kStaticWriteOnly.value));

// Xbox D3D addressing: same arithmetic as the guest lock 0x821A6D90.
static_assert(xbox_d3d::PhysicalToVirtual(0x00123000) == 0xC0123000);
static_assert(xbox_d3d::PhysicalToVirtual(0x1FF00000) == 0xDFF00000);
static_assert(xbox_d3d::PhysicalToVirtual(0xE0123000) == 0xC0124000);
// Runic depth inversion swaps only the ordered comparisons.
static_assert(RunicEffectiveDepthFunction(compare_function::kLess.value) ==
              compare_function::kGreater.value);
static_assert(RunicEffectiveDepthFunction(compare_function::kGreaterEqual.value) ==
              compare_function::kLessEqual.value);
static_assert(RunicEffectiveDepthFunction(compare_function::kEqual.value) ==
              compare_function::kEqual.value);
static_assert(SlotIs(kRenderSystemSlots, VtableSlot{104, Confidence::kConfirmed},
                     "setInvertVertexWinding"));
static_assert(kRenderSystemSlots[104].guest_impl == 0x8219BEB0);
static_assert(viewport::kActLeft.offset < viewport::kUpdated.offset);

int failures = 0;

void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

}  // namespace

int main() {
  // A fake guest address space: an object at 0x10 whose vptr points at a vtable at 0x40.
  uint8_t mem[0x200] = {};
  const uint8_t obj[] = {0x00, 0x00, 0x00, 0x40,  // vptr
                         0x12, 0x34, 0x56, 0x78,  // +4
                         0x3F, 0x80, 0x00, 0x00,  // +8: 1.0f
                         0xBE, 0xEF, 0x01, 0x00,  // +0xC: u16 0xBEEF, bool true, bool false
                         0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};  // +0x10: u64
  std::memcpy(mem + 0x10, obj, sizeof(obj));
  const uint32_t slot87 = 0x40 + render_system_vtable::kRender.byte_offset();
  mem[slot87 + 0] = 0x82;
  mem[slot87 + 1] = 0x1C;
  mem[slot87 + 2] = 0x40;
  mem[slot87 + 3] = 0x58;

  Check(ReadU32(mem, 0x10) == 0x40, "ReadU32 vptr");
  Check(ReadU32(mem, 0x10, Field{4, Confidence::kConfirmed}) == 0x12345678, "ReadU32 field");
  Check(ReadF32(mem, 0x10, Field{8, Confidence::kConfirmed}) == 1.0f, "ReadF32");
  Check(ReadU16(mem, 0x10, Field{0xC, Confidence::kConfirmed}) == 0xBEEF, "ReadU16");
  Check(ReadU8(mem, 0x1E) == 0x01, "ReadU8");
  Check(ReadBool(mem, 0x10, Field{0xE, Confidence::kConfirmed}), "ReadBool true");
  Check(!ReadBool(mem, 0x10, Field{0xF, Confidence::kConfirmed}), "ReadBool false");
  Check(ReadU64(mem, 0x10, Field{0x10, Confidence::kConfirmed}) == 0x0102030405060708ull,
        "ReadU64");
  Check(ReadVirtual(mem, 0x10, render_system_vtable::kRender) == 0x821C4058, "ReadVirtual");

  // Guest String: inline (capacity < 16) and heap forms.
  uint8_t str_mem[0x100] = {};
  std::memcpy(str_mem + 0x10, "abc", 3);
  str_mem[0x10 + 0x13] = 3;   // length (BE u32 at +0x10)
  str_mem[0x10 + 0x17] = 15;  // capacity
  Check(ReadString(str_mem, 0x10) == "abc", "ReadString inline");
  str_mem[0x40 + 0x3] = 0x80;  // pointer to 0x80
  std::memcpy(str_mem + 0x80, "texture_name.dds", 16);
  str_mem[0x40 + 0x13] = 16;
  str_mem[0x40 + 0x17] = 31;
  Check(ReadString(str_mem, 0x40) == "texture_name.dds", "ReadString heap");

  if (failures == 0) std::printf("guest_abi layout test: ok\n");
  return failures == 0 ? 0 : 1;
}
