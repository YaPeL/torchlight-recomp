// Static ABI map of the guest's OGRE: class layouts and vtable slots.
//
// The guest runs OGRE 1.7.0 "Cthugha" (Root ctor sub_824246A8 builds the version string
// from 1, 7, 0 and "Cthugha" @0x820DAB28) built by Runic for Xbox 360, with local changes.
// Reference headers: ~/ogre-1.7.0 (OGRECave tag v1-7-0). Header citations below are relative
// to OgreMain/include (or RenderSystems/Direct3D9/include for D3D9 classes).
//
// ABI facts every entry relies on:
//  - 32-bit pointers, big-endian, natural PPC alignment, MSVC class layout.
//  - Overloaded virtuals are grouped at the first declaration in reverse declaration order
//    (checked on RenderSystem slots 38/39, 44/45, 77/78, 96/97 and HardwareBuffer 8/9).
//  - SharedPtr<T> is polymorphic in 1.7 (virtual ~SharedPtr, OgreSharedPtr.h:151) and
//    OGRE_THREAD_SUPPORT is 0, so SharedPtr = {vptr, pRep, pUseCount, useFreeMethod}.
//  - STLAllocator has a virtual destructor (OgreMemorySTLAllocator.h:107), so every container
//    stores 4-byte allocator instances. The XDK STL puts them after the data pointers.
//  - Functions returning a class by value take the return slot in r3 and `this` in r4.
//
// Confidence per entry:
//  kConfirmed        offset/slot seen in a real access in recompiled code (cited).
//  kInferred         derived from the 1.7.0 header and neighbouring confirmed entries only.
//  kRunicDifference  disagrees with the 1.7.0 header; documented as observed, not forced.
//
// Nothing here dereferences guest memory on its own; all reads go through the helpers below.

#pragma once

#include <bit>
#include <cstdint>
#include <cstring>
#include <string>

#include "guest_abi/xbox_memory.h"

namespace torchlight::guest_abi {

enum class Confidence : uint8_t {
  kConfirmed,
  kInferred,
  kRunicDifference,
};

// Byte offset of a member inside a guest object.
struct Field {
  uint32_t offset;
  Confidence confidence;
};

// sizeof of a guest class.
struct Size {
  uint32_t bytes;
  Confidence confidence;
};

struct VtableSlot {
  uint32_t index;
  Confidence confidence;
  constexpr uint32_t byte_offset() const { return index * 4; }
};

// One row of a full vtable map. `guest_impl` is the function the listed guest vtable
// dispatches to for that slot.
struct SlotInfo {
  uint32_t index;
  const char* name;
  uint32_t guest_impl;
  Confidence confidence;
  const char* evidence;
};

// ---------------------------------------------------------------------------------------------
// Big-endian guest memory reads. `membase` is the host address of guest address 0; addresses are
// translated by xbox_memory::HostAddress.

inline uint8_t ReadU8(const uint8_t* membase, uint32_t addr) {
  return *xbox_memory::HostAddress(membase, addr);
}

inline uint16_t ReadU16(const uint8_t* membase, uint32_t addr) {
  uint16_t v;
  std::memcpy(&v, xbox_memory::HostAddress(membase, addr), sizeof(v));
  return std::endian::native == std::endian::big ? v : std::byteswap(v);
}

inline uint32_t ReadU32(const uint8_t* membase, uint32_t addr) {
  uint32_t v;
  std::memcpy(&v, xbox_memory::HostAddress(membase, addr), sizeof(v));
  return std::endian::native == std::endian::big ? v : std::byteswap(v);
}

inline uint64_t ReadU64(const uint8_t* membase, uint32_t addr) {
  uint64_t v;
  std::memcpy(&v, xbox_memory::HostAddress(membase, addr), sizeof(v));
  return std::endian::native == std::endian::big ? v : std::byteswap(v);
}

inline float ReadF32(const uint8_t* membase, uint32_t addr) {
  return std::bit_cast<float>(ReadU32(membase, addr));
}

inline bool ReadBool(const uint8_t* membase, uint32_t addr) { return ReadU8(membase, addr) != 0; }

// Big-endian guest memory write (for the few hooks that adjust guest data before the original
// function reads it).
inline void WriteU32(uint8_t* membase, uint32_t addr, uint32_t value) {
  if constexpr (std::endian::native != std::endian::big) value = std::byteswap(value);
  std::memcpy(xbox_memory::HostAddress(membase, addr), &value, sizeof(value));
}
inline void WriteBytes(uint8_t* membase, uint32_t addr, const void* data, size_t size) {
  std::memcpy(xbox_memory::HostAddress(membase, addr), data, size);
}

inline uint8_t ReadU8(const uint8_t* membase, uint32_t obj, Field f) {
  return ReadU8(membase, obj + f.offset);
}
inline uint16_t ReadU16(const uint8_t* membase, uint32_t obj, Field f) {
  return ReadU16(membase, obj + f.offset);
}
inline uint32_t ReadU32(const uint8_t* membase, uint32_t obj, Field f) {
  return ReadU32(membase, obj + f.offset);
}
inline uint64_t ReadU64(const uint8_t* membase, uint32_t obj, Field f) {
  return ReadU64(membase, obj + f.offset);
}
inline float ReadF32(const uint8_t* membase, uint32_t obj, Field f) {
  return ReadF32(membase, obj + f.offset);
}
inline bool ReadBool(const uint8_t* membase, uint32_t obj, Field f) {
  return ReadBool(membase, obj + f.offset);
}

// Address of the function a guest object dispatches to for `slot`.
inline uint32_t ReadVirtual(const uint8_t* membase, uint32_t obj, VtableSlot slot) {
  return ReadU32(membase, ReadU32(membase, obj) + slot.byte_offset());
}

namespace ogre {

// ---------------------------------------------------------------------------------------------
// Containers and smart pointers (XDK STL + OGRE 1.7 allocators)

namespace shared_ptr {
// [confirmed] OgreSharedPtr.h:151 virtual dtor. IndexData::indexBuffer.pRep is read at
// IndexData+4 in D3D9RenderSystem::_render 0x821C4058 @0x821C42D0.
inline constexpr Field kVptr{0x00, Confidence::kConfirmed};
// [confirmed] OgreSharedPtr.h:63. Same access as above; also map node value+4 read at
// D3D9RenderSystem::setVertexBufferBinding 0x821C3E78 @0x821C3F18.
inline constexpr Field kPRep{0x04, Confidence::kConfirmed};
// [confirmed] OgreSharedPtr.h:64. _setTexture 0x821C8E40 @0x821C8E84 bumps *[ptr+8].
inline constexpr Field kPUseCount{0x08, Confidence::kConfirmed};
// [inferred] OgreSharedPtr.h:65.
inline constexpr Field kUseFreeMethod{0x0C, Confidence::kInferred};
// [confirmed] IndexData::indexStart follows indexBuffer at +0x10 (see index_data).
inline constexpr Size kSize{0x10, Confidence::kConfirmed};
}  // namespace shared_ptr

namespace stl_vector {
// [confirmed] VertexDeclaration::findElementsBySource 0x824773C0 builds a returned vector:
// zeroes +0/+4/+8 and stores the STLAllocator vptr 0x820DED34 at +0xC (@0x824773DC).
// VertexDeclaration::getVertexSize 0x82477430 walks [+0, +4) (@0x8247743C, @0x82477444).
inline constexpr Field kFirst{0x00, Confidence::kConfirmed};
inline constexpr Field kLast{0x04, Confidence::kConfirmed};
inline constexpr Field kEnd{0x08, Confidence::kConfirmed};
inline constexpr Field kAllocator{0x0C, Confidence::kConfirmed};
inline constexpr Size kSize{0x10, Confidence::kConfirmed};
}  // namespace stl_vector

namespace stl_tree {
// std::map / std::set. [confirmed] VertexBufferBinding map ctor 0x82478050: allocates the head
// node into +4 (@0x82478090), zeroes the size at +8 (@0x8247807C), stores the node and value
// allocator vptrs at +0xC/+0x10 (@0x82478070, @0x82478074). +0 is the (empty) comparator.
inline constexpr Field kHead{0x04, Confidence::kConfirmed};
inline constexpr Field kSize{0x08, Confidence::kConfirmed};
inline constexpr Size kSizeof{0x14, Confidence::kConfirmed};
// Node: _Left, _Parent, _Right, then the value. [confirmed] HardwareBufferManagerBase
// _notifyVertexBufferDestroyed 0x824213C8 walks set<ptr> nodes with key at +0xC (@0x824213FC)
// and _Isnil at +0x11 (@0x824213F0). map<u16, SharedPtr> nodes are 0x24 bytes (new @0x82478084)
// with key at +0xC and value at +0x10 (D3D9RenderSystem::setVertexBufferBinding @0x821C3EC4,
// @0x821C3F18).
inline constexpr Field kNodeLeft{0x00, Confidence::kConfirmed};
inline constexpr Field kNodeParent{0x04, Confidence::kConfirmed};
inline constexpr Field kNodeRight{0x08, Confidence::kConfirmed};
inline constexpr Field kNodeKey{0x0C, Confidence::kConfirmed};
// Value of a map whose key is <= 4 bytes and whose value is 4-byte aligned.
inline constexpr Field kNodeValue{0x10, Confidence::kConfirmed};
}  // namespace stl_tree

namespace stl_string {
// [confirmed] Texture ctor 0x82655770 initialises mFSAAHint (Texture+0xDC) as an empty
// string: buf[0]=0 (@0x826557EC), size +0x10 = 0 (@0x826557E8), capacity +0x14 = 15
// (@0x826557E4). The next Texture member sits 0x1C after the string's start.
inline constexpr Field kBuffer{0x00, Confidence::kConfirmed};
inline constexpr Field kLength{0x10, Confidence::kConfirmed};
inline constexpr Field kCapacity{0x14, Confidence::kConfirmed};
inline constexpr Size kSize{0x1C, Confidence::kConfirmed};
inline constexpr uint32_t kInlineCapacity = 15;
}  // namespace stl_string

// Contents of a guest String at `str`. [confirmed] c_str() rule: capacity < 16 keeps the text
// inline at +0, otherwise +0 holds a pointer (e.g. sub_82191EA8 @0x82192078: lwz 0x14,
// cmplwi 0x10, blt -> inline, else lwz 0(r3); 2386 such sites). `max_length` bounds a corrupt
// length.
inline std::string ReadString(const uint8_t* membase, uint32_t str, uint32_t max_length = 4096) {
  uint32_t length = ReadU32(membase, str + stl_string::kLength.offset);
  uint32_t capacity = ReadU32(membase, str + stl_string::kCapacity.offset);
  if (length > max_length) length = max_length;
  uint32_t data = capacity > stl_string::kInlineCapacity ? ReadU32(membase, str) : str;
  const uint8_t* text = xbox_memory::HostAddress(membase, data);
  return std::string(reinterpret_cast<const char*>(text), length);
}

// A NUL-terminated guest char string at `address` (bytes as they are in memory), at most
// `max_length` bytes; empty for a null address.
inline std::string ReadCString(const uint8_t* membase, uint32_t address,
                               uint32_t max_length = 4096) {
  std::string s;
  if (!address) return s;
  const uint8_t* text = xbox_memory::HostAddress(membase, address);
  for (uint32_t i = 0; i < max_length && text[i]; ++i) s.push_back(static_cast<char>(text[i]));
  return s;
}

// ---------------------------------------------------------------------------------------------
// RenderOperation (non-polymorphic)

namespace render_operation {
// [confirmed] OgreRenderOperation.h:63. D3D9RenderSystem::_render 0x821C4058 @0x821C4064.
inline constexpr Field kVertexData{0x00, Confidence::kConfirmed};
// [confirmed] OgreRenderOperation.h:66. D3D9RenderSystem::_render @0x821C40B8 (switch op-1).
inline constexpr Field kOperationType{0x04, Confidence::kConfirmed};
// [confirmed] OgreRenderOperation.h:72 (bool). D3D9RenderSystem::_render @0x821C40E4 (lbz).
inline constexpr Field kUseIndexes{0x08, Confidence::kConfirmed};
// [confirmed] OgreRenderOperation.h:75. D3D9RenderSystem::_render @0x821C40F4.
inline constexpr Field kIndexData{0x0C, Confidence::kConfirmed};
// [inferred] OgreRenderOperation.h:77; the only header member left before +0x14.
inline constexpr Field kSrcRenderable{0x10, Confidence::kInferred};
// [runic] Not in 1.7.0. RenderSystem::_render 0x821CEA60 multiplies the primitive count
// (@0x821CEAA0) and the vertex count (@0x821CEB18) by it. Inline RenderOperation ctors set it
// to 1 next to operationType=OT_TRIANGLE_LIST and useIndexes=true (@0x821C5FE8, @0x8245039C,
// @0x824C063C). The name is a hypothesis by analogy with OGRE 1.8's
// RenderOperation::numberOfInstances. Static search found no writer storing a constant > 1
// into a confirmed RenderOperation; the other writers copy loaded values (open question).
inline constexpr Field kRunicInstanceCount{0x14, Confidence::kRunicDifference};
}  // namespace render_operation

// ---------------------------------------------------------------------------------------------
// VertexData / IndexData (non-polymorphic)

namespace vertex_data {
// [inferred] OgreVertexIndexData.h:56 (private mMgr, new in 1.7); consistent with the members
// below.
inline constexpr Field kMgr{0x00, Confidence::kInferred};
// [confirmed] OgreVertexIndexData.h:79. D3D9RenderSystem::_render @0x821C4090 passes it to
// setVertexDeclaration (vt+0x150).
inline constexpr Field kVertexDeclaration{0x04, Confidence::kConfirmed};
// [confirmed] OgreVertexIndexData.h:83. D3D9RenderSystem::_render @0x821C40AC passes it to
// setVertexBufferBinding (vt+0x154).
inline constexpr Field kVertexBufferBinding{0x08, Confidence::kConfirmed};
// [inferred] OgreVertexIndexData.h:85 (bool).
inline constexpr Field kDeleteDclBinding{0x0C, Confidence::kInferred};
// [confirmed] OgreVertexIndexData.h:87. D3D9RenderSystem::_render @0x821C43EC passes it to the
// indexed draw.
inline constexpr Field kVertexStart{0x10, Confidence::kConfirmed};
// [confirmed] OgreVertexIndexData.h:89. D3D9RenderSystem::_render @0x821C4070: the early-out
// guard `*(op+0) -> +0x14 != 0` (0x821C4058, recorded by the legacy port as a "guard before
// _render") is vertexData->vertexCount != 0. Also RenderSystem::_render @0x821CEB0C.
inline constexpr Field kVertexCount{0x14, Confidence::kConfirmed};
}  // namespace vertex_data

namespace index_data {
// [confirmed] OgreVertexIndexData.h:250 (HardwareIndexBufferSharedPtr). pRep read at +4 in
// D3D9RenderSystem::_render @0x821C42D0.
inline constexpr Field kIndexBuffer{0x00, Confidence::kConfirmed};
// [confirmed] OgreVertexIndexData.h:253. D3D9RenderSystem::_render @0x821C43E8.
inline constexpr Field kIndexStart{0x10, Confidence::kConfirmed};
// [confirmed] OgreVertexIndexData.h:256. D3D9RenderSystem::_render @0x821C40F8,
// RenderSystem::_render @0x821CEA90.
inline constexpr Field kIndexCount{0x14, Confidence::kConfirmed};
}  // namespace index_data

// ---------------------------------------------------------------------------------------------
// VertexElement (non-polymorphic) / VertexDeclaration

namespace vertex_element {
// [confirmed] OgreHardwareVertexBuffer.h:139 (u16). getVertexSize 0x82477430 @0x82477454 (lhz).
inline constexpr Field kSource{0x00, Confidence::kConfirmed};
// [confirmed] OgreHardwareVertexBuffer.h:141. addElement 0x82477038 @0x824770A0 builds the
// element with offset at +4; D3D9 declaration build 0x821CE5E0 @0x821CE6C0.
inline constexpr Field kOffset{0x04, Confidence::kConfirmed};
// [confirmed] OgreHardwareVertexBuffer.h:143. getVertexSize @0x82477460; D3D9 declaration
// build @0x821CE6D0 switches on it.
inline constexpr Field kType{0x08, Confidence::kConfirmed};
// [confirmed] OgreHardwareVertexBuffer.h:145. findElementBySemantic 0x82477380 @0x82477390.
inline constexpr Field kSemantic{0x0C, Confidence::kConfirmed};
// [confirmed] OgreHardwareVertexBuffer.h:147 (u16). findElementBySemantic @0x8247739C (lhz).
inline constexpr Field kIndex{0x10, Confidence::kConfirmed};
// [confirmed] element stride in every walk, e.g. getVertexSize @0x8247746C (addi 0x14).
inline constexpr Size kSize{0x14, Confidence::kConfirmed};
}  // namespace vertex_element

namespace vertex_declaration {
// [runic] OgreHardwareVertexBuffer.h:309 declares VertexElementList as list<VertexElement>;
// the guest uses a contiguous vector<VertexElement> (STLAllocator vptr 0x820DED34).
// HardwareBufferManagerBase::createVertexDeclarationImpl 0x824208A0 constructs it at +4
// (@0x824208D8..@0x824208E4); addElement push_back @0x824770B4 and back() @0x824770B8.
inline constexpr Field kElementList{0x04, Confidence::kRunicDifference};
// [confirmed] `new` of 0x14 in createVertexDeclarationImpl @0x824208AC; D3D9VertexDeclaration
// puts its D3D9Resource base at +0x14 (RTTI COL offset 20 for vtable 0x820F6674).
inline constexpr Size kSize{0x14, Confidence::kConfirmed};
}  // namespace vertex_declaration

namespace d3d9_vertex_declaration {
// [confirmed] D3D9 declaration lookup 0x821CE5E0 finds by device in the map at +0x18 and
// compares with its head at +0x1C (@0x821CE610).
inline constexpr Field kDeviceToDeclarationMap{0x18, Confidence::kConfirmed};
}  // namespace d3d9_vertex_declaration

// ---------------------------------------------------------------------------------------------
// VertexBufferBinding

namespace vertex_buffer_binding {
// [confirmed] OgreHardwareVertexBuffer.h:489, map<u16, HardwareVertexBufferSharedPtr>.
// getBindings (slot 4) 0x82590780 returns this+4; ctor at createVertexBufferBindingImpl
// 0x82420910 @0x82420950.
inline constexpr Field kBindingMap{0x04, Confidence::kConfirmed};
// [confirmed] OgreHardwareVertexBuffer.h:490 (u16). getNextIndex 0x82476CD0 @0x82476CD4,
// ctor @0x82420964.
inline constexpr Field kHighIndex{0x18, Confidence::kConfirmed};
// [confirmed] `new` of 0x1C in createVertexBufferBindingImpl @0x82420920.
inline constexpr Size kSize{0x1C, Confidence::kConfirmed};
}  // namespace vertex_buffer_binding

// ---------------------------------------------------------------------------------------------
// HardwareBuffer / HardwareVertexBuffer / HardwareIndexBuffer

namespace hardware_buffer {
// [confirmed] OgreHardwareBuffer.h:140. DefaultHardwareIndexBuffer ctor (inline in
// 0x82423560) stores itemSize*count @0x824235F4; copyData(src) 0x82423220 reads both @0x8242322C.
inline constexpr Field kSizeInBytes{0x04, Confidence::kConfirmed};
// [confirmed] OgreHardwareBuffer.h:141. Ctor @0x8242359C; D3D9HardwareVertexBuffer::lockImpl
// tests its bits @0x821A7A90.
inline constexpr Field kUsage{0x08, Confidence::kConfirmed};
// [confirmed] OgreHardwareBuffer.h:142 (bool). HardwareBuffer::lock 0x821A7928 sets it
// @0x821A795C; unlock 0x821A7AF0 clears it @0x821A7B18.
inline constexpr Field kIsLocked{0x0C, Confidence::kConfirmed};
// [confirmed] OgreHardwareBuffer.h:143. HardwareBuffer::lock @0x821A7954.
inline constexpr Field kLockStart{0x10, Confidence::kConfirmed};
// [confirmed] OgreHardwareBuffer.h:144. HardwareBuffer::lock @0x821A7958.
inline constexpr Field kLockSize{0x14, Confidence::kConfirmed};
// [confirmed] OgreHardwareBuffer.h:145 (bool). Default buffer ctors store true @0x824235C0.
inline constexpr Field kSystemMemory{0x18, Confidence::kConfirmed};
// [confirmed] OgreHardwareBuffer.h:146 (bool). Default buffer ctors store false @0x824235C4.
inline constexpr Field kUseShadowBuffer{0x19, Confidence::kConfirmed};
// [runic] OgreHardwareBuffer.h:147-149 declare mpShadowBuffer, mShadowUpdated and
// mSuppressHardwareUpdate; the guest has none of them. No ctor initialises them, lock
// 0x821A7928 has no shadow path, _updateFromShadow is an empty function (0x825BC790), and the
// subclasses start their own members at +0x1C.
inline constexpr Size kSize{0x1C, Confidence::kRunicDifference};
}  // namespace hardware_buffer

namespace hardware_vertex_buffer {
// [confirmed] OgreHardwareVertexBuffer.h:51. ~HardwareVertexBuffer 0x82476CE8 notifies the
// manager read @0x82476D00; DefaultHardwareVertexBuffer ctor (inline in 0x824234A8) @0x824234EC.
inline constexpr Field kMgr{0x1C, Confidence::kConfirmed};
// [confirmed] OgreHardwareVertexBuffer.h:52. DefaultHardwareVertexBuffer ctor @0x824234F4;
// HardwareBufferManagerBase::makeBufferCopy 0x82421498 @0x824214C0.
inline constexpr Field kNumVertices{0x20, Confidence::kConfirmed};
// [confirmed] OgreHardwareVertexBuffer.h:53. DefaultHardwareVertexBuffer ctor @0x824234FC;
// makeBufferCopy @0x824214C4; D3D9RenderSystem::setVertexBufferBinding @0x821C3F2C.
inline constexpr Field kVertexSize{0x24, Confidence::kConfirmed};
// [runic] Not in 1.7.0. DefaultHardwareVertexBuffer ctor stores false (byte) @0x82423510.
// Hypothesis by analogy with OGRE 1.8's HardwareVertexBuffer::mIsInstanceData.
inline constexpr Field kRunicIsInstanceData{0x28, Confidence::kRunicDifference};
// [runic] Not in 1.7.0. DefaultHardwareVertexBuffer ctor stores 1 @0x82423514.
// Hypothesis by analogy with OGRE 1.8's HardwareVertexBuffer::mInstanceDataStepRate.
inline constexpr Field kRunicInstanceDataStepRate{0x2C, Confidence::kRunicDifference};
// [confirmed] DefaultHardwareVertexBuffer::mpData lands at +0x30 (@0x82423528) and
// D3D9HardwareVertexBuffer's D3D9Resource base is at +0x30 (RTTI COL offset 48).
inline constexpr Size kSize{0x30, Confidence::kConfirmed};
}  // namespace hardware_vertex_buffer

namespace hardware_index_buffer {
// [confirmed] OgreHardwareIndexBuffer.h:55. DefaultHardwareIndexBuffer ctor (inline in
// 0x82423560) @0x824235A4.
inline constexpr Field kMgr{0x1C, Confidence::kConfirmed};
// [confirmed] OgreHardwareIndexBuffer.h:56. Ctor @0x824235AC; IT_16BIT -> 2, IT_32BIT -> 4.
inline constexpr Field kIndexType{0x20, Confidence::kConfirmed};
// [confirmed] OgreHardwareIndexBuffer.h:57. Ctor @0x824235C8.
inline constexpr Field kNumIndexes{0x24, Confidence::kConfirmed};
// [confirmed] OgreHardwareIndexBuffer.h:58. Ctor @0x824235E0.
inline constexpr Field kIndexSize{0x28, Confidence::kConfirmed};
// [confirmed] DefaultHardwareIndexBuffer::mpData lands at +0x2C (@0x82423604) and
// D3D9HardwareIndexBuffer's D3D9Resource base is at +0x2C (RTTI COL offset 44).
inline constexpr Size kSize{0x2C, Confidence::kConfirmed};
}  // namespace hardware_index_buffer

namespace default_hardware_vertex_buffer {
// [confirmed] CPU backing store. Ctor allocates it @0x82423528; ~ frees it 0x82423280
// @0x82423298.
inline constexpr Field kData{0x30, Confidence::kConfirmed};
// [confirmed] `new` of 0x34 in DefaultHardwareBufferManagerBase::createVertexBuffer @0x824234BC.
inline constexpr Size kSize{0x34, Confidence::kConfirmed};
}  // namespace default_hardware_vertex_buffer

namespace default_hardware_index_buffer {
// [confirmed] CPU backing store, allocated @0x82423604.
inline constexpr Field kData{0x2C, Confidence::kConfirmed};
// [confirmed] `new` of 0x30 in DefaultHardwareBufferManagerBase::createIndexBuffer @0x82423574.
inline constexpr Size kSize{0x30, Confidence::kConfirmed};
}  // namespace default_hardware_index_buffer

// D3D9 buffers keep a map<IDirect3DDevice9*, BufferResources*> after their D3D9Resource base
// (OgreD3D9HardwareVertexBuffer.h, 1.7.0) and an optional system-memory copy. When the copy
// exists, lockImpl returns `mSystemMemoryBuffer + offset` and marks every device's resources
// out of date; otherwise it locks the device buffer.
namespace d3d9_hardware_vertex_buffer {
// [confirmed] D3D9HardwareVertexBuffer::lockImpl 0x821A7968 reads the map head at +0x38
// (@0x821A799C); D3D9RenderSystem::setVertexBufferBinding passes +0x34 to map::find
// (@0x821C3F20).
inline constexpr Field kDeviceToResourcesMap{0x34, Confidence::kConfirmed};
// [confirmed] lockImpl @0x821A7974 (null test) and @0x821A7A60 (returns it + offset). This is
// the "HardwareVertexBuffer+96 backing storage" of the legacy port. Runic slot 4 (see
// hardware_buffer_vtable) frees it @0x82582CEC.
inline constexpr Field kSystemMemoryBuffer{0x60, Confidence::kConfirmed};
// [confirmed] `new` of 0x64 in D3D9HardwareBufferManagerBase::createVertexBuffer @0x82580FBC.
inline constexpr Size kSize{0x64, Confidence::kConfirmed};
}  // namespace d3d9_hardware_vertex_buffer

namespace d3d9_hardware_index_buffer {
// [confirmed] D3D9HardwareIndexBuffer::lockImpl 0x825821C8 reads the head at +0x34
// (@0x825821FC); D3D9RenderSystem::_render passes +0x30 to map::find (@0x821C42D4).
inline constexpr Field kDeviceToResourcesMap{0x30, Confidence::kConfirmed};
// [confirmed] lockImpl @0x825821D4; D3D9RenderSystem::_render @0x821C4324.
inline constexpr Field kSystemMemoryBuffer{0x58, Confidence::kConfirmed};
// [confirmed] `new` of 0x5C in D3D9HardwareBufferManagerBase::createIndexBuffer @0x825810A0.
inline constexpr Size kSize{0x5C, Confidence::kConfirmed};
}  // namespace d3d9_hardware_index_buffer

namespace d3d9_buffer_resources {
// Value of the device map above. [confirmed] lockImpl 0x821A7968: device buffer locked from +0
// (@0x821A7AD4), out-of-date flag @0x821A79BC, lock window @0x821A79C8/@0x821A79B8 and lock
// options @0x821A7A38.
inline constexpr Field kBuffer{0x00, Confidence::kConfirmed};
inline constexpr Field kOutOfDate{0x04, Confidence::kConfirmed};
inline constexpr Field kLockOffset{0x08, Confidence::kConfirmed};
inline constexpr Field kLockLength{0x0C, Confidence::kConfirmed};
inline constexpr Field kLockOptions{0x10, Confidence::kConfirmed};
}  // namespace d3d9_buffer_resources

// ---------------------------------------------------------------------------------------------
// Texture (derives from Resource, which derives from StringInterface)

namespace resource {
// [confirmed] Resource::getName (Texture vtable slot 22, 0x824E5010) returns this+0x2C.
inline constexpr Field kName{0x2C, Confidence::kConfirmed};
// [confirmed] getHandle (slot 23, 0x824312E0) loads a u64 from +0x68.
inline constexpr Field kHandle{0x68, Confidence::kConfirmed};
// [confirmed] getLoadingState (slot 27, 0x821910A8) loads +0x70.
inline constexpr Field kLoadingState{0x70, Confidence::kConfirmed};
// [confirmed] isManuallyLoaded (slot 18, 0x824312D8) loads the byte at +0x7C.
inline constexpr Field kIsManual{0x7C, Confidence::kConfirmed};
// [confirmed] Texture's first own member is at +0xB8 (Texture ctor 0x82655770 @0x8265579C).
inline constexpr Size kSize{0xB8, Confidence::kConfirmed};
}  // namespace resource

namespace gpu_program {
// [confirmed] GpuProgram::getType (vtable slot 50, vt+0xC8, called by D3D9RenderSystem::
// bindGpuProgram @0x821C13B4) is 0x824E9258, which returns [this+0xB8]: the first member after
// Resource. The name is Resource::mName (resource::kName).
inline constexpr Field kType{0xB8, Confidence::kConfirmed};
// [confirmed] GpuProgram::mSource (String; OgreGpuProgram.h, after mType and mFilename):
// D3D9HLSLProgram::loadFromSource 0x82587FB0 passes [this+0xD8] and its length [this+0xE8] as the
// first two D3DXCompileShader arguments (@0x825881F0, @0x825881F4).
inline constexpr Field kSource{0xD8, Confidence::kConfirmed};
}  // namespace gpu_program

namespace d3d_gamma_ramp {
// Xbox D3D gamma ramp as read by the writers (guest_functions.h), 0x600 bytes, 16-bit values.
// [confirmed] Table form (D3DGAMMARAMP): red[256] at +0, green[256] at +0x200, blue[256] at +0x400
// (sub_82776500 reads +2i, +0x202, +0x402 relative to r4 - 2: @0x82776564..@0x82776574).
// [confirmed] PWL form (D3DPWLGAMMA): 128 entries of {u16 base, u16 delta} per channel at the
// same offsets (sub_827765F8 @0x8277666C..@0x827766A8; linear builder sub_82776730).
inline constexpr Field kRed{0x000, Confidence::kConfirmed};
inline constexpr Field kGreen{0x200, Confidence::kConfirmed};
inline constexpr Field kBlue{0x400, Confidence::kConfirmed};
inline constexpr Size kSize{0x600, Confidence::kConfirmed};
inline constexpr uint32_t kTableEntries = 256;
inline constexpr uint32_t kPwlEntries = 128;
}  // namespace d3d_gamma_ramp

namespace high_level_gpu_program {
// [confirmed] HighLevelGpuProgram::mAssemblerProgram (GpuProgramPtr), the program the render
// system binds: the D3D9HLSLProgram ctor 0x82588AB0 builds the SharedPtr there (vptr 0x820D8768
// @0x82588AE8, pRep/useCount/freeMethod zeroed @0x82588AEC..@0x82588AFC), and
// _getBindingDelegate (vtable 0x82004734 slot 51, 0x821CB800) returns [this+0x188].
inline constexpr Field kAssemblerProgram{0x184, Confidence::kConfirmed};
}  // namespace high_level_gpu_program

namespace d3d9_hlsl_program {
// [confirmed] D3D9HLSLProgram members, from the D3DXCompileShader call in loadFromSource
// 0x82587FB0 (pFunctionName = r7, pProfile = r8): mTarget +0x198 (@0x825881C0), mEntryPoint
// +0x1B4 (@0x825881D4). mPreprocessorDefines +0x1D0 is split for the defines (@0x82588000).
inline constexpr Field kTarget{0x198, Confidence::kConfirmed};
inline constexpr Field kEntryPoint{0x1B4, Confidence::kConfirmed};
inline constexpr Field kPreprocessorDefines{0x1D0, Confidence::kConfirmed};
}  // namespace d3d9_hlsl_program

namespace render_target {
// [confirmed] RenderTarget vtable 0x8211398C: getName (slot 1, 0x82590780) returns this+4,
// getWidth (slot 3) loads +0x24, getHeight (slot 4) +0x28, getColourDepth (slot 5) +0x2C.
inline constexpr Field kName{0x04, Confidence::kConfirmed};
inline constexpr Field kWidth{0x24, Confidence::kConfirmed};
inline constexpr Field kHeight{0x28, Confidence::kConfirmed};
inline constexpr Field kColourDepth{0x2C, Confidence::kConfirmed};
// [confirmed] D3D9RenderSystem::_setViewport 0x821A4368 calls vt+0xA0 to decide the flipped
// viewport origin (@0x821A43F4); _setCullingMode uses it the same way (@0x821C6D20).
inline constexpr VtableSlot kRequiresTextureFlipping{40, Confidence::kConfirmed};
}  // namespace render_target

namespace viewport {
// All [confirmed] in D3D9RenderSystem::_setViewport 0x821A4368: target @0x821A43A0, actual
// rectangle @0x821A43CC..@0x821A43DC, updated flag @0x821A4388 (cleared @0x821A4460).
inline constexpr Field kTarget{0x14, Confidence::kConfirmed};
inline constexpr Field kActLeft{0x28, Confidence::kConfirmed};
inline constexpr Field kActTop{0x2C, Confidence::kConfirmed};
inline constexpr Field kActWidth{0x30, Confidence::kConfirmed};
inline constexpr Field kActHeight{0x34, Confidence::kConfirmed};
inline constexpr Field kUpdated{0x5C, Confidence::kConfirmed};
}  // namespace viewport

namespace render_system {
// Members of the guest D3D9RenderSystem read at draw time.
// [confirmed] _setCullingMode 0x821C6CF8 reads the active render target here (@0x821C6D14).
inline constexpr Field kActiveRenderTarget{0x38, Confidence::kConfirmed};
// [confirmed] _setViewport compares/stores the active viewport (@0x821A4374, @0x821A4394).
inline constexpr Field kActiveViewport{0x70, Confidence::kConfirmed};
// [confirmed] _setCullingMode stores the requested mode (@0x821C6D0C).
inline constexpr Field kCullingMode{0x74, Confidence::kConfirmed};
// [confirmed] setInvertVertexWinding (slot 104, 0x8219BEB0) writes this byte; its getters are the
// folded slots 103/105. _setCullingMode reads it (@0x821C6D38).
inline constexpr Field kInvertVertexWinding{0x290, Confidence::kConfirmed};
// [confirmed] setCurrentPassIterationCount (slot 112, 0x821CEF38).
inline constexpr Field kCurrentPassIterationCount{0x298, Confidence::kConfirmed};
// [confirmed] setDeriveDepthBias (slot 113, 0x821CEF20): flag byte and base/multiplier/slope.
inline constexpr Field kDeriveDepthBias{0x2A0, Confidence::kConfirmed};
inline constexpr Field kDeriveDepthBiasBase{0x2A4, Confidence::kConfirmed};
inline constexpr Field kDeriveDepthBiasMultiplier{0x2A8, Confidence::kConfirmed};
inline constexpr Field kDeriveDepthBiasSlopeScale{0x2AC, Confidence::kConfirmed};
}  // namespace render_system

namespace matrix4 {
// [confirmed] 16 contiguous floats, OGRE row-major m[row][col]: the D3D9 conversion 0x824639B8
// reads +0x00..+0x3C and writes the transpose.
inline constexpr Size kSize{0x40, Confidence::kConfirmed};
}  // namespace matrix4

namespace colour_value {
// [inferred] OgreColourValue.h: float r, g, b, a with no vptr.
inline constexpr Field kR{0x00, Confidence::kInferred};
inline constexpr Field kG{0x04, Confidence::kInferred};
inline constexpr Field kB{0x08, Confidence::kInferred};
inline constexpr Field kA{0x0C, Confidence::kInferred};
}  // namespace colour_value

namespace uvw_addressing_mode {
// [confirmed] D3D9RenderSystem::_setTextureAddressingMode 0x821CA1A8 reads u @0x821CA1B4,
// v @0x821CA274 and w @0x821CA314.
inline constexpr Field kU{0x00, Confidence::kConfirmed};
inline constexpr Field kV{0x04, Confidence::kConfirmed};
inline constexpr Field kW{0x08, Confidence::kConfirmed};
}  // namespace uvw_addressing_mode

namespace layer_blend_mode_ex {
// D3D9RenderSystem::_setTextureBlendMode 0x821C9100 (unit r4, LayerBlendModeEx& r5).
// OgreBlendMode.h:139..160. [confirmed] blendType compared with 0/1 @0x821C910C; source1/source2
// compared with LBS_MANUAL @0x821C9234/@0x821C9330; colourArg1 @0x821C9134..; colourArg2
// @0x821C9258..; alphaArg1 @0x821C91CC; alphaArg2 @0x821C92E8. [inferred] operation and factor.
inline constexpr Field kBlendType{0x00, Confidence::kConfirmed};
inline constexpr Field kOperation{0x04, Confidence::kInferred};
inline constexpr Field kSource1{0x08, Confidence::kConfirmed};
inline constexpr Field kSource2{0x0C, Confidence::kConfirmed};
inline constexpr Field kColourArg1{0x10, Confidence::kConfirmed};
inline constexpr Field kColourArg2{0x20, Confidence::kConfirmed};
inline constexpr Field kAlphaArg1{0x30, Confidence::kConfirmed};
inline constexpr Field kAlphaArg2{0x34, Confidence::kConfirmed};
inline constexpr Field kFactor{0x38, Confidence::kInferred};
}  // namespace layer_blend_mode_ex

namespace plane {
// [confirmed] size: setClipPlanesImpl 0x82573870 walks the PlaneList with stride 0x10
// (@0x8257389C). [inferred] order from OgrePlane.h: Vector3 normal, Real d.
inline constexpr Size kSize{0x10, Confidence::kConfirmed};
inline constexpr Field kNormal{0x00, Confidence::kInferred};
inline constexpr Field kD{0x0C, Confidence::kInferred};
}  // namespace plane

namespace texture {
// All [confirmed] by Texture ctor 0x82655770 defaults (OgreTexture.cpp) and by the Texture
// vtable getters/setters listed in kTextureSlots.
// OgreTexture.h:378. ctor stores 512 @0x8265579C; getHeight 0x824E9258.
inline constexpr Field kHeight{0xB8, Confidence::kConfirmed};
// OgreTexture.h:379. ctor stores 512 @0x826557A4; getWidth 0x8257A3A0.
inline constexpr Field kWidth{0xBC, Confidence::kConfirmed};
// OgreTexture.h:380. ctor stores 1 @0x826557C0; getDepth 0x8257A3A8.
inline constexpr Field kDepth{0xC0, Confidence::kConfirmed};
// OgreTexture.h:382. setNumMipmaps 0x8257A350 writes it with mNumMipmaps.
inline constexpr Field kNumRequestedMipmaps{0xC4, Confidence::kConfirmed};
// OgreTexture.h:383. getNumMipmaps 0x8257A348.
inline constexpr Field kNumMipmaps{0xC8, Confidence::kConfirmed};
// OgreTexture.h:384 (bool). getMipmapsHardwareGenerated 0x823643B0.
inline constexpr Field kMipmapsHardwareGenerated{0xCC, Confidence::kConfirmed};
// OgreTexture.h:385 (float). ctor stores 1.0f @0x826557B0; getGamma 0x8257A360.
inline constexpr Field kGamma{0xD0, Confidence::kConfirmed};
// OgreTexture.h:386 (bool). isHardwareGammaEnabled 0x8235FE90.
inline constexpr Field kHwGamma{0xD4, Confidence::kConfirmed};
// OgreTexture.h:387. getFSAA 0x8257A398.
inline constexpr Field kFSAA{0xD8, Confidence::kConfirmed};
// OgreTexture.h:388 (String). getFSAAHint 0x825A6A78 returns this+0xDC.
inline constexpr Field kFSAAHint{0xDC, Confidence::kConfirmed};
// OgreTexture.h:390. ctor stores TEX_TYPE_2D (2) @0x826557F0; getTextureType 0x821C08B0.
inline constexpr Field kTextureType{0xF8, Confidence::kConfirmed};
// OgreTexture.h:391. getFormat 0x82378568.
inline constexpr Field kFormat{0xFC, Confidence::kConfirmed};
// OgreTexture.h:392. ctor stores TU_DEFAULT (0x105) @0x826557F8; getUsage 0x825004F8.
inline constexpr Field kUsage{0x100, Confidence::kConfirmed};
// OgreTexture.h:394. getSrcFormat 0x82500518.
inline constexpr Field kSrcFormat{0x104, Confidence::kConfirmed};
// OgreTexture.h:395. getSrcWidth 0x82500538.
inline constexpr Field kSrcWidth{0x108, Confidence::kConfirmed};
// OgreTexture.h:395. getSrcHeight 0x82382F60.
inline constexpr Field kSrcHeight{0x10C, Confidence::kConfirmed};
// OgreTexture.h:395. getSrcDepth 0x822BD5E8.
inline constexpr Field kSrcDepth{0x110, Confidence::kConfirmed};
// OgreTexture.h:397. getDesiredFormat 0x82378560.
inline constexpr Field kDesiredFormat{0x114, Confidence::kConfirmed};
// OgreTexture.h:398 (u16). getDesiredIntegerBitDepth 0x82655AC0.
inline constexpr Field kDesiredIntegerBitDepth{0x118, Confidence::kConfirmed};
// OgreTexture.h:399 (u16). getDesiredFloatBitDepth 0x82655AD0.
inline constexpr Field kDesiredFloatBitDepth{0x11A, Confidence::kConfirmed};
// OgreTexture.h:400 (bool). getTreatLuminanceAsAlpha 0x82655AF0.
inline constexpr Field kTreatLuminanceAsAlpha{0x11C, Confidence::kConfirmed};
// OgreTexture.h:402 (bool). ctor clears it @0x8265581C.
inline constexpr Field kInternalResourcesCreated{0x11D, Confidence::kConfirmed};
// [confirmed] D3D9Texture's D3D9Resource base is at +0x120 (RTTI COL offset 288); the D3D9Texture
// ctor 0x8257A400 stores that vptr @0x8257A460.
inline constexpr Size kSize{0x120, Confidence::kConfirmed};
}  // namespace texture

namespace d3d9_texture {
// [confirmed] map<IDirect3DDevice9*, TextureResources*>. D3D9Texture::getTexture 0x821C9058
// passes +0x124 to map::find (@0x821C9068) and compares with the head at +0x128 (@0x821C9088).
inline constexpr Field kDeviceToTextureResourcesMap{0x124, Confidence::kConfirmed};
// [confirmed] vector<HardwarePixelBufferSharedPtr> (stride 0x10). Runic slot 72 impl 0x8257B6E0
// walks it @0x8257B714; ctor stores its allocator vptr @0x8257A4C8.
inline constexpr Field kSurfaceList{0x138, Confidence::kConfirmed};
}  // namespace d3d9_texture

namespace d3d9_texture_resources {
// [inferred] OgreD3D9Texture.h, TextureResources {pNormTex, pCubeTex, pVolumeTex, pBaseTex,
// pFSAASurface}. Only pBaseTex is read: getTexture 0x821C9058 returns [res+0xC] (@0x821C90F4).
inline constexpr Field kNormTex{0x00, Confidence::kInferred};
inline constexpr Field kCubeTex{0x04, Confidence::kInferred};
inline constexpr Field kVolumeTex{0x08, Confidence::kInferred};
inline constexpr Field kBaseTex{0x0C, Confidence::kConfirmed};
}  // namespace d3d9_texture_resources

// ---------------------------------------------------------------------------------------------
// GpuProgramParameters (non-polymorphic)

namespace gpu_program_parameters {
// [confirmed] OgreGpuProgramParams.h:1113 (vector<float>). D3D9RenderSystem::
// bindGpuProgramParameters 0x821C2118 reads _Myfirst @0x821C222C.
inline constexpr Field kFloatConstants{0x00, Confidence::kConfirmed};
// [confirmed] OgreGpuProgramParams.h:1115 (vector<int>). Same function @0x821C22A8.
inline constexpr Field kIntConstants{0x10, Confidence::kConfirmed};
// [confirmed] OgreGpuProgramParams.h:1118 (GpuLogicalBufferStructPtr). Same function copies
// the SharedPtr from +0x20 (@0x821C219C).
inline constexpr Field kFloatLogicalToPhysical{0x20, Confidence::kConfirmed};
// [confirmed] OgreGpuProgramParams.h:1121. Same function copies it from +0x30 (@0x821C21AC).
inline constexpr Field kIntLogicalToPhysical{0x30, Confidence::kConfirmed};
// [inferred] OgreGpuProgramParams.h:1123 (GpuNamedConstantsPtr), between two confirmed fields.
inline constexpr Field kNamedConstants{0x40, Confidence::kInferred};
// [confirmed] OgreGpuProgramParams.h:1125 (vector<AutoConstantEntry>). _updateAutoParams
// 0x82201EB0 reads first/last @0x82201EC4/@0x82201ECC.
inline constexpr Field kAutoConstants{0x50, Confidence::kConfirmed};
// [confirmed] OgreGpuProgramParams.h:1127 (u16). _updateAutoParams @0x82201EE0.
inline constexpr Field kCombinedVariability{0x60, Confidence::kConfirmed};
// [confirmed] OgreGpuProgramParams.h:1129 (bool). _writeRawConstant(Matrix4) 0x821C2868 reads it
// (@0x821C2874) and transposes the matrix (0x824639B8) when set; it differs per parameters object.
inline constexpr Field kTransposeMatrices{0x62, Confidence::kConfirmed};
// [inferred] OgreGpuProgramParams.h:1131 (bool).
inline constexpr Field kIgnoreMissingParams{0x63, Confidence::kInferred};
// [confirmed] OgreGpuProgramParams.h:1133. _updateAutoParams stores -1 @0x82201F00 and the
// ACT_PASS_ITERATION_NUMBER entry's physical index @0x82202E18.
inline constexpr Field kActivePassIterationIndex{0x64, Confidence::kConfirmed};
// [confirmed] OgreGpuProgramParams.h:1147 (vector<GpuSharedParametersUsage>, stride 0x30).
// bindGpuProgramParameters walks it for GPV_GLOBAL @0x821C216C..@0x821C2188.
inline constexpr Field kSharedParamSets{0x68, Confidence::kConfirmed};
// [inferred] OgreGpuProgramParams.h:1150 (Any: vptr + content pointer).
inline constexpr Field kRenderSystemData{0x78, Confidence::kInferred};
}  // namespace gpu_program_parameters

namespace auto_constant_entry {
// All [confirmed] in _updateAutoParams 0x82201EB0. OgreGpuProgramParams.h:1079..1092.
// paramType: switch index @0x82201F34.
inline constexpr Field kParamType{0x00, Confidence::kConfirmed};
// physicalIndex: @0x82202DF8, @0x82204230.
inline constexpr Field kPhysicalIndex{0x04, Confidence::kConfirmed};
// elementCount: @0x82204220.
inline constexpr Field kElementCount{0x08, Confidence::kConfirmed};
// data / fData union: @0x82204218.
inline constexpr Field kData{0x0C, Confidence::kConfirmed};
// variability (u16): @0x82201F24.
inline constexpr Field kVariability{0x10, Confidence::kConfirmed};
// Loop stride @0x82204244.
inline constexpr Size kSize{0x14, Confidence::kConfirmed};
}  // namespace auto_constant_entry

namespace gpu_logical_buffer_struct {
// [confirmed] OgreGpuProgramParams.h:354 (map<size_t, GpuLogicalIndexUse>); no mutex because
// threading is off. bindGpuProgramParameters reads the head at pRep+4 (@0x821C21D4).
inline constexpr Field kMap{0x00, Confidence::kConfirmed};
}  // namespace gpu_logical_buffer_struct

namespace gpu_logical_index_use_node {
// Map node of the map above. [confirmed] bindGpuProgramParameters 0x821C2118: logical index
// (key) @0x821C2204, physicalIndex @0x821C220C, currentSize @0x821C2200, variability (u16)
// @0x821C21F0. OgreGpuProgramParams.h:337/339/341.
inline constexpr Field kLogicalIndex{0x0C, Confidence::kConfirmed};
inline constexpr Field kPhysicalIndex{0x10, Confidence::kConfirmed};
inline constexpr Field kCurrentSize{0x14, Confidence::kConfirmed};
inline constexpr Field kVariability{0x18, Confidence::kConfirmed};
}  // namespace gpu_logical_index_use_node

// ---------------------------------------------------------------------------------------------
// Vtables

// [confirmed] Located through MSVC RTTI (vtable[-1] -> Complete Object Locator -> TypeDescriptor).
inline constexpr uint32_t kRenderSystemVtable = 0x820E92CC;            // .?AVRenderSystem@Ogre@@
inline constexpr uint32_t kD3D9RenderSystemVtable = 0x82005794;        // .?AVD3D9RenderSystem@Ogre@@
inline constexpr uint32_t kHardwareBufferVtable = 0x820DF798;
inline constexpr uint32_t kHardwareVertexBufferVtable = 0x820DF730;
inline constexpr uint32_t kHardwareIndexBufferVtable = 0x820E3EAC;
inline constexpr uint32_t kD3D9HardwareVertexBufferVtable = 0x8200118C;
inline constexpr uint32_t kD3D9HardwareIndexBufferVtable = 0x820F6914;
inline constexpr uint32_t kDefaultHardwareVertexBufferVtable = 0x820DAA24;
inline constexpr uint32_t kDefaultHardwareIndexBufferVtable = 0x820DAA58;
inline constexpr uint32_t kVertexDeclarationVtable = 0x820DF764;
inline constexpr uint32_t kD3D9VertexDeclarationVtable = 0x820F6640;
inline constexpr uint32_t kVertexBufferBindingVtable = 0x8200139C;
inline constexpr uint32_t kHardwareBufferManagerBaseVtable = 0x820DA84C;
inline constexpr uint32_t kD3D9HardwareBufferManagerBaseVtable = 0x82002164;
inline constexpr uint32_t kD3D9HardwareBufferManagerVtable = 0x820020FC;
inline constexpr uint32_t kTextureVtable = 0x82113434;
inline constexpr uint32_t kD3D9TextureVtable = 0x820049F4;
// The shared _purecall thunk and the shared empty function (`blr`).
inline constexpr uint32_t kPureCall = 0x8285C8B0;
inline constexpr uint32_t kEmptyFunction = 0x825BC790;

namespace render_system_vtable {
// Slot order: OgreRenderSystem.h (1.7.0) declaration order with MSVC overload grouping, plus a
// Runic virtual at 89 that shifts every later slot by one. 127 slots = 125 header + 1 Runic +
// D3D9RenderSystem::initConfigOptions. Full map with evidence: kRenderSystemSlots.
inline constexpr VtableSlot kSetTextureByName{38, Confidence::kConfirmed};
inline constexpr VtableSlot kSetTexture{39, Confidence::kConfirmed};
inline constexpr VtableSlot kBeginFrame{55, Confidence::kConfirmed};
inline constexpr VtableSlot kEndFrame{58, Confidence::kConfirmed};
inline constexpr VtableSlot kSetViewport{59, Confidence::kInferred};
inline constexpr VtableSlot kSetVertexDeclaration{84, Confidence::kConfirmed};
inline constexpr VtableSlot kSetVertexBufferBinding{85, Confidence::kConfirmed};
inline constexpr VtableSlot kRender{87, Confidence::kConfirmed};
inline constexpr VtableSlot kRunicSlot89_UnknownStringGetter{89, Confidence::kRunicDifference};
inline constexpr VtableSlot kBindGpuProgram{90, Confidence::kConfirmed};
inline constexpr VtableSlot kBindGpuProgramParameters{91, Confidence::kConfirmed};
inline constexpr VtableSlot kBindGpuProgramPassIterationParameters{92, Confidence::kConfirmed};
inline constexpr VtableSlot kUnbindGpuProgram{93, Confidence::kConfirmed};
inline constexpr VtableSlot kClearFrameBuffer{107, Confidence::kConfirmed};
inline constexpr VtableSlot kSetRenderTarget{114, Confidence::kConfirmed};
inline constexpr VtableSlot kSetClipPlanesImpl{124, Confidence::kConfirmed};
inline constexpr uint32_t kSlotCount = 127;
}  // namespace render_system_vtable

// Every slot of the guest D3D9RenderSystem vtable (0x82005794). `guest_impl` is the D3D9
// dispatch target. Evidence: header line plus, for confirmed slots, what the function does.
inline constexpr SlotInfo kRenderSystemSlots[] = {
    {0, "~dtor", 0x8256E4E8, Confidence::kInferred, "OgreRenderSystem.h:130"},
    {1, "getName", 0x8256E738, Confidence::kInferred, "OgreRenderSystem.h:134"},
    {2, "getConfigOptions", 0x8256FE98, Confidence::kInferred, "OgreRenderSystem.h:157"},
    {3, "setConfigOption", 0x8256F510, Confidence::kInferred, "OgreRenderSystem.h:178"},
    {4, "createHardwareOcclusionQuery", 0x82573BD0, Confidence::kInferred, "OgreRenderSystem.h:182"},
    {5, "destroyHardwareOcclusionQuery", 0x824C3FA8, Confidence::kInferred, "OgreRenderSystem.h:186"},
    {6, "validateConfigOptions", 0x8256FB90, Confidence::kInferred, "OgreRenderSystem.h:192"},
    {7, "_initialise", 0x8256FEA0, Confidence::kInferred, "OgreRenderSystem.h:210"},
    {8, "createRenderSystemCapabilities", 0x82574168, Confidence::kInferred, "OgreRenderSystem.h:214"},
    {9, "useCustomRenderSystemCapabilities", 0x824C34D0, Confidence::kInferred, "OgreRenderSystem.h:222"},
    {10, "reinitialise", 0x82570BC0, Confidence::kInferred, "OgreRenderSystem.h:226"},
    {11, "shutdown", 0x82570C70, Confidence::kInferred, "OgreRenderSystem.h:230"},
    {12, "setAmbientLight", 0x825BC790, Confidence::kInferred, "OgreRenderSystem.h:235"},
    {13, "setShadingType", 0x825BC790, Confidence::kInferred, "OgreRenderSystem.h:239"},
    {14, "setLightingEnabled", 0x825BC790, Confidence::kInferred, "OgreRenderSystem.h:246"},
    {15, "_createRenderWindow", 0x82570E40, Confidence::kInferred, "OgreRenderSystem.h:455"},
    {16, "_createRenderWindows", 0x825711E0, Confidence::kInferred, "OgreRenderSystem.h:473"},
    {17, "createMultiRenderTarget", 0x82572788, Confidence::kInferred, "OgreRenderSystem.h:481"},
    {18, "destroyRenderWindow", 0x824C3778, Confidence::kInferred, "OgreRenderSystem.h:484"},
    {19, "destroyRenderTexture", 0x824C3778, Confidence::kInferred, "OgreRenderSystem.h:486"},
    {20, "destroyRenderTarget", 0x82572858, Confidence::kInferred, "OgreRenderSystem.h:488"},
    {21, "attachRenderTarget", 0x824C37D0, Confidence::kInferred, "OgreRenderSystem.h:492"},
    {22, "getRenderTarget", 0x824C38D8, Confidence::kInferred, "OgreRenderSystem.h:496"},
    {23, "detachRenderTarget", 0x824C3928, Confidence::kInferred, "OgreRenderSystem.h:502"},
    {24, "getRenderTargetIterator", 0x824C2F80, Confidence::kInferred, "OgreRenderSystem.h:508"},
    {25, "getErrorDescription", 0x82572938, Confidence::kInferred, "OgreRenderSystem.h:513"},
    {26, "_useLights", 0x82572A50, Confidence::kInferred, "OgreRenderSystem.h:544"},
    {27, "areFixedFunctionLightsInViewSpace", 0x828AD748, Confidence::kInferred, "OgreRenderSystem.h:547"},
    {28, "_setWorldMatrix", 0x821DD450, Confidence::kInferred, "OgreRenderSystem.h:549"},
    {29, "_setWorldMatrices", 0x824C3CB8, Confidence::kInferred, "OgreRenderSystem.h:551"},
    {30, "_setViewMatrix", 0x821BAC40, Confidence::kInferred, "OgreRenderSystem.h:553"},
    {31, "_setProjectionMatrix", 0x821BDC50, Confidence::kInferred, "OgreRenderSystem.h:555"},
    {32, "_setTextureUnitSettings", 0x821C93A0, Confidence::kInferred, "OgreRenderSystem.h:561"},
    {33, "_disableTextureUnit", 0x821DC8D8, Confidence::kInferred, "OgreRenderSystem.h:563"},
    {34, "_disableTextureUnitsFrom", 0x821BF850, Confidence::kInferred, "OgreRenderSystem.h:565"},
    {35, "_setSurfaceParams", 0x825BC790, Confidence::kInferred, "OgreRenderSystem.h:599"},
    {36, "_setPointSpritesEnabled", 0x821D2530, Confidence::kInferred, "OgreRenderSystem.h:609"},
    {37, "_setPointParameters", 0x821C56A0, Confidence::kInferred, "OgreRenderSystem.h:621"},
    {38, "_setTexture", 0x824C39D8, Confidence::kConfirmed, "OgreRenderSystem.h:652; looks the name up in TextureManager (vcall @0x824C3A14) and delegates"},
    {39, "_setTexture", 0x821C8E40, Confidence::kConfirmed, "OgreRenderSystem.h:637; D3D9 impl: getTexture 0x821C9058, getTextureType via vt+0xB0"},
    {40, "_setVertexTexture", 0x821CA710, Confidence::kInferred, "OgreRenderSystem.h:663"},
    {41, "_setTextureCoordSet", 0x821CAD88, Confidence::kInferred, "OgreRenderSystem.h:674"},
    {42, "_setTextureCoordCalculation", 0x821CB0F0, Confidence::kInferred, "OgreRenderSystem.h:683"},
    {43, "_setTextureBlendMode", 0x821C9100, Confidence::kConfirmed, "OgreRenderSystem.h:692; reads LayerBlendModeEx fields (see layer_blend_mode_ex) and compares sources with LBS_MANUAL"},
    {44, "_setTextureUnitFiltering", 0x821CA470, Confidence::kConfirmed, "OgreRenderSystem.h:708; references string D3D9RenderSystem::_setTextureUnitFiltering @0x821CA4F8"},
    {45, "_setTextureUnitFiltering", 0x821CA3F0, Confidence::kConfirmed, "OgreRenderSystem.h:700; calls vt+0xB0 (slot 44) three times with FT_MIN/MAG/MIP"},
    {46, "_setTextureLayerAnisotropy", 0x821CAD08, Confidence::kInferred, "OgreRenderSystem.h:711"},
    {47, "_setTextureAddressingMode", 0x821CA1A8, Confidence::kInferred, "OgreRenderSystem.h:714"},
    {48, "_setTextureBorderColour", 0x82572D58, Confidence::kInferred, "OgreRenderSystem.h:717"},
    {49, "_setTextureMipmapBias", 0x821CA880, Confidence::kInferred, "OgreRenderSystem.h:727"},
    {50, "_setTextureMatrix", 0x821CA930, Confidence::kInferred, "OgreRenderSystem.h:733"},
    {51, "_setSceneBlending", 0x821C52B0, Confidence::kInferred, "OgreRenderSystem.h:745"},
    {52, "_setSeparateSceneBlending", 0x82572E30, Confidence::kInferred, "OgreRenderSystem.h:759"},
    {53, "_setAlphaRejectSettings", 0x821C54D8, Confidence::kInferred, "OgreRenderSystem.h:768"},
    {54, "_setTextureProjectionRelativeTo", 0x8219B038, Confidence::kInferred, "OgreRenderSystem.h:773"},
    {55, "_beginFrame", 0x821AE5A8, Confidence::kConfirmed, "OgreRenderSystem.h:778; references string D3D9RenderSystem::_beginFrame @0x821AE5D0"},
    {56, "_pauseFrame", 0x82573538, Confidence::kInferred, "OgreRenderSystem.h:788"},
    {57, "_resumeFrame", 0x82573688, Confidence::kInferred, "OgreRenderSystem.h:795"},
    {58, "_endFrame", 0x821B1000, Confidence::kConfirmed, "OgreRenderSystem.h:800; references the _endFrame string inside sub_821B1000"},
    {59, "_setViewport", 0x821A4368, Confidence::kInferred, "OgreRenderSystem.h:808"},
    {60, "_getViewport", 0x821910A8, Confidence::kInferred, "OgreRenderSystem.h:810"},
    {61, "_setCullingMode", 0x821C6CF8, Confidence::kConfirmed, "OgreRenderSystem.h:823; stores the mode at +0x74 and maps 1/2/3 to D3DCULL; _setViewport re-applies it via vt+0xF4 (@0x821A43C0)"},
    {62, "_getCullingMode", 0x82204268, Confidence::kInferred, "OgreRenderSystem.h:825"},
    {63, "_setDepthBufferParams", 0x821BE2F8, Confidence::kInferred, "OgreRenderSystem.h:840"},
    {64, "_setDepthBufferCheckEnabled", 0x821C2F38, Confidence::kInferred, "OgreRenderSystem.h:846"},
    {65, "_setDepthBufferWriteEnabled", 0x821C4E88, Confidence::kInferred, "OgreRenderSystem.h:851"},
    {66, "_setDepthBufferFunction", 0x821C3010, Confidence::kInferred, "OgreRenderSystem.h:859"},
    {67, "_setColourBufferWriteEnabled", 0x821C3520, Confidence::kInferred, "OgreRenderSystem.h:867"},
    {68, "_setDepthBias", 0x821D14B8, Confidence::kConfirmed, "OgreRenderSystem.h:890; D3D9 _render calls it via vt+0x110 with mDerive* (+0x2A4..+0x2AC)"},
    {69, "_setFog", 0x825BC790, Confidence::kInferred, "OgreRenderSystem.h:902"},
    {70, "_beginGeometryCount", 0x821F28F8, Confidence::kConfirmed, "OgreRenderSystem.h:906; zeroes +0x84/+0x88/+0x8C"},
    {71, "_getFaceCount", 0x821F1790, Confidence::kConfirmed, "OgreRenderSystem.h:908; lwz +0x88 (mFaceCount)"},
    {72, "_getBatchCount", 0x8219AE58, Confidence::kConfirmed, "OgreRenderSystem.h:910; lwz +0x84 (mBatchCount)"},
    {73, "_getVertexCount", 0x824C3CB0, Confidence::kConfirmed, "OgreRenderSystem.h:912; lwz +0x8C (mVertexCount)"},
    {74, "convertColourValue", 0x821DB560, Confidence::kInferred, "OgreRenderSystem.h:922"},
    {75, "getColourVertexElementType", 0x8257F850, Confidence::kConfirmed, "OgreRenderSystem.h:926; VertexDeclaration::addElement calls it via vt+0x12C @0x82477080 for VET_COLOUR"},
    {76, "_convertProjectionMatrix", 0x8219AD88, Confidence::kInferred, "OgreRenderSystem.h:934"},
    {77, "_makeProjectionMatrix", 0x82573AB0, Confidence::kConfirmed, "OgreRenderSystem.h:952; frustum overload: left/right/bottom/top in f1..f4"},
    {78, "_makeProjectionMatrix", 0x82572970, Confidence::kConfirmed, "OgreRenderSystem.h:943; fovy overload: Radian& in r4, aspect in f1"},
    {79, "_makeOrthoMatrix", 0x821ABAE8, Confidence::kInferred, "OgreRenderSystem.h:960"},
    {80, "_applyObliqueDepthProjection", 0x82573C80, Confidence::kInferred, "OgreRenderSystem.h:979"},
    {81, "_setPolygonMode", 0x821C5228, Confidence::kInferred, "OgreRenderSystem.h:983"},
    {82, "setStencilCheckEnabled", 0x825730A0, Confidence::kInferred, "OgreRenderSystem.h:991"},
    {83, "setStencilBufferParams", 0x825730F8, Confidence::kInferred, "OgreRenderSystem.h:1044"},
    {84, "setVertexDeclaration", 0x821CE5A0, Confidence::kConfirmed, "OgreRenderSystem.h:1054; D3D9 _render @0x821C408C via vt+0x150 with vertexData->vertexDeclaration"},
    {85, "setVertexBufferBinding", 0x821C3E78, Confidence::kConfirmed, "OgreRenderSystem.h:1056; D3D9 _render @0x821C40A8 via vt+0x154; walks mBindingMap"},
    {86, "setNormaliseNormals", 0x825BC790, Confidence::kInferred, "OgreRenderSystem.h:1068"},
    {87, "_render", 0x821C4058, Confidence::kConfirmed, "OgreRenderSystem.h:1082; D3D9 override (vertexCount==0 guard, calls base); base RenderSystem::_render = 0x821CEA60 in base vtable 0x820E92CC slot 87"},
    {88, "getDriverVersion", 0x824C2F98, Confidence::kConfirmed, "OgreRenderSystem.h:1090; returns this+0x304 (mDriverVersion)"},
    {89, "kRunicSlot89_UnknownStringGetter", 0x824C40E8, Confidence::kRunicDifference, "not in 1.7.0; same impl in base vtable 0x820E92CC; returns a const String& to static 0x8349D54C (empty in the image; unknown whether it is always empty); every later slot is header index + 1"},
    {90, "bindGpuProgram", 0x821C1398, Confidence::kConfirmed, "OgreRenderSystem.h:1096; switches on prg->getType() (vt+0xC8)"},
    {91, "bindGpuProgramParameters", 0x821C2118, Confidence::kConfirmed, "OgreRenderSystem.h:1103; mask==GPV_PASS_ITERATION_NUMBER(8) -> vt+0x170 (slot 92); mask&GPV_GLOBAL -> _copySharedParams"},
    {92, "bindGpuProgramPassIterationParameters", 0x82573730, Confidence::kConfirmed, "OgreRenderSystem.h:1108; pure in the base vtable (_purecall 0x8285C8B0)"},
    {93, "unbindGpuProgram", 0x821BE470, Confidence::kConfirmed, "OgreRenderSystem.h:1113; switches on gptype and unbinds"},
    {94, "isGpuProgramBound", 0x821BF120, Confidence::kConfirmed, "OgreRenderSystem.h:1116; returns the bool at +0x2E0..+0x2E2 by gptype"},
    {95, "setClipPlanes", 0x824C3D50, Confidence::kConfirmed, "OgreRenderSystem.h:1120; compares with mClipPlanes (+0x2E4), sets dirty (+0x2F4)"},
    {96, "addClipPlane", 0x824C3D10, Confidence::kConfirmed, "OgreRenderSystem.h:1125; builds a Plane on the stack and calls vt+0x184 (slot 97)"},
    {97, "addClipPlane", 0x824C3CD8, Confidence::kConfirmed, "OgreRenderSystem.h:1123; push_back into +0x2E4, sets dirty +0x2F4"},
    {98, "resetClipPlanes", 0x824C3DA0, Confidence::kConfirmed, "OgreRenderSystem.h:1129; clears +0x2E4 when non-empty, sets dirty +0x2F4"},
    {99, "_initRenderTargets", 0x824C3448, Confidence::kConfirmed, "OgreRenderSystem.h:1132; walks mRenderTargets (+0x14)"},
    {100, "_notifyCameraRemoved", 0x824C3DE8, Confidence::kConfirmed, "OgreRenderSystem.h:1137; walks mRenderTargets forwarding r4 (camera)"},
    {101, "_updateAllRenderTargets", 0x821EE1C0, Confidence::kConfirmed, "OgreRenderSystem.h:1140; walks mPrioritisedRenderTargets (+0x28) with a bool"},
    {102, "_swapAllRenderTargetBuffers", 0x821EE2F8, Confidence::kInferred, "OgreRenderSystem.h:1143"},
    {103, "getInvertVertexWinding", 0x824C3CD0, Confidence::kConfirmed, "OgreRenderSystem.h:1147; lbz +0x290 (mInvertVertexWinding)"},
    {104, "setInvertVertexWinding", 0x8219BEB0, Confidence::kConfirmed, "OgreRenderSystem.h:1151; stb +0x290"},
    {105, "getVertexWindingInverted", 0x824C3CD0, Confidence::kConfirmed, "OgreRenderSystem.h:1156; same code as slot 103 (COMDAT folded)"},
    {106, "setScissorTest", 0x825739A8, Confidence::kInferred, "OgreRenderSystem.h:1169"},
    {107, "clearFrameBuffer", 0x8219CAF8, Confidence::kConfirmed, "OgreRenderSystem.h:1179; maps FBT_* bits to D3DCLEAR flags"},
    {108, "getHorizontalTexelOffset", 0x82573C70, Confidence::kInferred, "OgreRenderSystem.h:1191"},
    {109, "getVerticalTexelOffset", 0x82573C70, Confidence::kInferred, "OgreRenderSystem.h:1201"},
    {110, "getMinimumDepthInputValue", 0x82455DF8, Confidence::kInferred, "OgreRenderSystem.h:1211"},
    {111, "getMaximumDepthInputValue", 0x82573D48, Confidence::kInferred, "OgreRenderSystem.h:1220"},
    {112, "setCurrentPassIterationCount", 0x821CEF38, Confidence::kConfirmed, "OgreRenderSystem.h:1226; stw +0x298 (mCurrentPassIterationCount)"},
    {113, "setDeriveDepthBias", 0x821CEF20, Confidence::kConfirmed, "OgreRenderSystem.h:1237; writes +0x2A0 (bool) and +0x2A4..+0x2AC (floats)"},
    {114, "_setRenderTarget", 0x821A7620, Confidence::kConfirmed, "OgreRenderSystem.h:1249; _setViewport 0x821A4368 calls it via vt+0x1C8 with viewport->mTarget (@0x821A43A8)"},
    {115, "addListener", 0x824C3E58, Confidence::kInferred, "OgreRenderSystem.h:1282"},
    {116, "removeListener", 0x824C3ED0, Confidence::kInferred, "OgreRenderSystem.h:1285"},
    {117, "getRenderSystemEvents", 0x824C2FA0, Confidence::kInferred, "OgreRenderSystem.h:1291"},
    {118, "preExtraThreadsStarted", 0x825BC790, Confidence::kInferred, "OgreRenderSystem.h:1309"},
    {119, "postExtraThreadsStarted", 0x825BC790, Confidence::kInferred, "OgreRenderSystem.h:1315"},
    {120, "registerThread", 0x825BC790, Confidence::kInferred, "OgreRenderSystem.h:1329"},
    {121, "unregisterThread", 0x825BC790, Confidence::kInferred, "OgreRenderSystem.h:1334"},
    {122, "getDisplayMonitorCount", 0x828AD7C0, Confidence::kConfirmed, "OgreRenderSystem.h:1340; returns 1"},
    {123, "fireEvent", 0x824C3F48, Confidence::kConfirmed, "OgreRenderSystem.h:1401; walks mEventListeners (+0x2C0)"},
    {124, "setClipPlanesImpl", 0x82573870, Confidence::kConfirmed, "OgreRenderSystem.h:1424; RenderSystem::_render 0x821CEA60 calls it via vt+0x1F0 with &mClipPlanes"},
    {125, "initialiseFromRenderSystemCapabilities", 0x82572528, Confidence::kInferred, "OgreRenderSystem.h:1427"},
    {126, "initConfigOptions", 0x8256E848, Confidence::kConfirmed, "OgreD3D9RenderSystem.h:190; D3D9-only virtual; builds the Full Screen/FSAA/Allow NVPerfHUD/Floating-point mode options"},
};

namespace hardware_buffer_vtable {
// OgreHardwareBuffer.h: lockImpl/unlockImpl are declared before the destructor, so the
// destructor is slot 2. Base vtable 0x820DF798 has pure 0, 1, 6, 7.
// [confirmed] lock 0x821A7928 calls slot 0 via vt+0 (@0x821A7944).
inline constexpr VtableSlot kLockImpl{0, Confidence::kConfirmed};
// [confirmed] unlock 0x821A7AF0 calls slot 1 via vt+4 (@0x821A7B08).
inline constexpr VtableSlot kUnlockImpl{1, Confidence::kConfirmed};
// [confirmed] ~HardwareVertexBuffer 0x82476CE8 / ~DefaultHardwareVertexBuffer 0x82423280
// (scalar deleting, flag in r4).
inline constexpr VtableSlot kDestructor{2, Confidence::kConfirmed};
// [confirmed] OgreHardwareBuffer.h:180. 0x821A7928; copyData 0x824231A0 calls it on the
// source via vt+0xC with HBL_READ_ONLY.
inline constexpr VtableSlot kLock{3, Confidence::kConfirmed};
// [runic] Not in 1.7.0. Empty in the base (0x825BC790); D3D9HardwareVertexBuffer 0x82582CD8
// and D3D9HardwareIndexBuffer 0x82582188 free and clear mSystemMemoryBuffer (+0x60 / +0x58).
inline constexpr VtableSlot kRunicSlot4_FreeSystemMemoryBuffer{4, Confidence::kRunicDifference};
// [confirmed] OgreHardwareBuffer.h:226. 0x821A7AF0; copyData calls it via vt+0x14.
inline constexpr VtableSlot kUnlock{5, Confidence::kConfirmed};
// [inferred] OgreHardwareBuffer.h:252; pure in the base.
inline constexpr VtableSlot kReadData{6, Confidence::kInferred};
// [confirmed] OgreHardwareBuffer.h:261. copyData 0x824231A0 calls it via vt+0x1C (@0x824231F8).
inline constexpr VtableSlot kWriteData{7, Confidence::kConfirmed};
// [confirmed] OgreHardwareBuffer.h:288 copyData(src). 0x82423220 clamps to both sizes and calls
// slot 9 via vt+0x24 (@0x82423260).
inline constexpr VtableSlot kCopyDataWhole{8, Confidence::kConfirmed};
// [confirmed] OgreHardwareBuffer.h:274 copyData(src, srcOff, dstOff, len, discard). 0x824231A0.
inline constexpr VtableSlot kCopyData{9, Confidence::kConfirmed};
// [inferred] OgreHardwareBuffer.h:295. Empty function in every guest buffer vtable.
inline constexpr VtableSlot kUpdateFromShadow{10, Confidence::kInferred};
inline constexpr uint32_t kSlotCount = 11;
// HardwareVertexBuffer and HardwareIndexBuffer add no virtuals.
}  // namespace hardware_buffer_vtable

namespace vertex_declaration_vtable {
// OgreHardwareVertexBuffer.h:317..444, plus one Runic slot. D3D9VertexDeclaration overrides 0-6.
inline constexpr VtableSlot kDestructor{0, Confidence::kInferred};
// [confirmed] :379. 0x82477038 pushes a 0x14-byte element (see vertex_element).
inline constexpr VtableSlot kAddElement{1, Confidence::kConfirmed};
inline constexpr VtableSlot kInsertElement{2, Confidence::kInferred};          // :394
inline constexpr VtableSlot kRemoveElementBySemantic{3, Confidence::kInferred};  // :407
inline constexpr VtableSlot kRemoveElementByIndex{4, Confidence::kInferred};     // :399
inline constexpr VtableSlot kRemoveAllElements{5, Confidence::kInferred};        // :410
inline constexpr VtableSlot kModifyElement{6, Confidence::kInferred};            // :417
// [confirmed] :425. 0x82477380 matches semantic +0xC and index +0x10.
inline constexpr VtableSlot kFindElementBySemantic{7, Confidence::kConfirmed};
// [confirmed] :435. 0x824773C0 returns a vector of elements whose source matches.
inline constexpr VtableSlot kFindElementsBySource{8, Confidence::kConfirmed};
// [confirmed] :438. 0x82477430 sums type sizes of elements with a given source.
inline constexpr VtableSlot kGetVertexSize{9, Confidence::kConfirmed};
// [runic] Not in 1.7.0. 0x82477748 counts elements with semantic VES_TEXTURE_COORDINATES.
inline constexpr VtableSlot kRunicSlot10_CountTexCoordElements{10, Confidence::kRunicDifference};
// [confirmed] :444. 0x82477490 creates the copy through mgr vt+0x34
// (HardwareBufferManagerBase::createVertexDeclaration, slot 13).
inline constexpr VtableSlot kClone{11, Confidence::kConfirmed};
inline constexpr uint32_t kSlotCount = 12;
}  // namespace vertex_declaration_vtable

namespace vertex_buffer_binding_vtable {
// OgreHardwareVertexBuffer.h:494..549; matches the header with no shift.
inline constexpr VtableSlot kDestructor{0, Confidence::kInferred};
inline constexpr VtableSlot kSetBinding{1, Confidence::kInferred};    // :503
inline constexpr VtableSlot kUnsetBinding{2, Confidence::kInferred};  // :505
inline constexpr VtableSlot kUnsetAllBindings{3, Confidence::kInferred};  // :508
// [confirmed] :511. 0x82590780 returns this+4; D3D9RenderSystem::setVertexBufferBinding calls
// it via vt+0x10 (@0x821C3E90).
inline constexpr VtableSlot kGetBindings{4, Confidence::kConfirmed};
inline constexpr VtableSlot kGetBuffer{5, Confidence::kInferred};  // :514
// [confirmed] :516. 0x82477A08 compares map::find with end.
inline constexpr VtableSlot kIsBufferBound{6, Confidence::kConfirmed};
// [confirmed] :518. 0x824E49B0 returns the map size at +0xC.
inline constexpr VtableSlot kGetBufferCount{7, Confidence::kConfirmed};
// [confirmed] :525. 0x82476CD0 returns mHighIndex++ (+0x18).
inline constexpr VtableSlot kGetNextIndex{8, Confidence::kConfirmed};
// [confirmed] :529. 0x82477A58 returns 0 for an empty map, else the last key + 1.
inline constexpr VtableSlot kGetLastBoundIndex{9, Confidence::kConfirmed};
inline constexpr VtableSlot kHasGaps{10, Confidence::kInferred};    // :535
inline constexpr VtableSlot kCloseGaps{11, Confidence::kInferred};  // :549
inline constexpr uint32_t kSlotCount = 12;
}  // namespace vertex_buffer_binding_vtable

namespace hardware_buffer_manager_vtable {
// HardwareBufferManagerBase (OgreHardwareBufferManager.h:109) and the HardwareBufferManager
// facade (:384) share the slot order; the facade forwards each call to mImpl (+0x84). Runic
// inserted two virtuals before createVertexBuffer, so it is slot 10 instead of 8.
// [runic] 0x822AA9F8 returns [this+0x20], the size of the second set (index buffers?).
inline constexpr VtableSlot kRunicSlot8_SecondSetSize{8, Confidence::kRunicDifference};
// [runic] 0x824E49B0 returns [this+0xC], the size of the first set (vertex buffers?).
inline constexpr VtableSlot kRunicSlot9_FirstSetSize{9, Confidence::kRunicDifference};
// [confirmed] :240. Pure in the base. makeBufferCopy 0x82421498 calls vt+0x28 with
// (vertexSize, numVertices, usage, shadow) @0x824214BC. D3D9HardwareBufferManagerBase impl
// 0x82580F68; D3D9HardwareBufferManager facade impl 0x8220C808 forwards via mImpl vt+0x28.
// Returns a SharedPtr by value: r3 = return slot, r4 = this, r5.. = arguments.
inline constexpr VtableSlot kCreateVertexBuffer{10, Confidence::kConfirmed};
// [confirmed] :259. Pure in the base. D3D9HardwareBufferManagerBase impl 0x82581058 allocates
// a 0x5C D3D9HardwareIndexBuffer; facade impl 0x8220C7C8. Same calling convention as above.
inline constexpr VtableSlot kCreateIndexBuffer{11, Confidence::kConfirmed};
inline constexpr uint32_t kD3D9BaseCreateVertexBuffer = 0x82580F68;
inline constexpr uint32_t kD3D9BaseCreateIndexBuffer = 0x82581058;
inline constexpr uint32_t kD3D9FacadeCreateVertexBuffer = 0x8220C808;
inline constexpr uint32_t kD3D9FacadeCreateIndexBuffer = 0x8220C7C8;
}  // namespace hardware_buffer_manager_vtable

namespace texture_vtable {
// StringInterface (0-4), Resource (5-42), Texture (43-89). One Runic pure virtual at 72
// shifts getFormat and later slots by one. Full map with evidence: kTextureSlots.
inline constexpr VtableSlot kLoad{15, Confidence::kConfirmed};
// Runic signature: touch(bool) loads only when the argument is true (see kTextureSlots[21]).
inline constexpr VtableSlot kTouch{21, Confidence::kConfirmed};
inline constexpr VtableSlot kGetName{22, Confidence::kConfirmed};
inline constexpr VtableSlot kGetTextureType{44, Confidence::kConfirmed};
inline constexpr VtableSlot kGetNumMipmaps{45, Confidence::kConfirmed};
inline constexpr VtableSlot kGetHeight{55, Confidence::kConfirmed};
inline constexpr VtableSlot kGetWidth{56, Confidence::kConfirmed};
inline constexpr VtableSlot kGetDepth{57, Confidence::kConfirmed};
inline constexpr VtableSlot kGetUsage{64, Confidence::kConfirmed};
inline constexpr VtableSlot kCreateInternalResources{66, Confidence::kInferred};
inline constexpr VtableSlot kFreeInternalResources{67, Confidence::kConfirmed};
inline constexpr VtableSlot kRunicSlot72_ReleaseSurfaces{72, Confidence::kRunicDifference};
inline constexpr VtableSlot kGetFormat{73, Confidence::kConfirmed};
inline constexpr VtableSlot kGetBuffer{86, Confidence::kConfirmed};
inline constexpr uint32_t kSlotCount = 90;
}  // namespace texture_vtable

// Every slot of the guest Texture vtable (0x82113434). `guest_impl` is the Texture-level
// function (D3D9Texture 0x820049F4 overrides some of them, e.g. 72 -> 0x8257B6E0).
inline constexpr SlotInfo kTextureSlots[] = {
    {0, "~dtor", 0x826558A8, Confidence::kConfirmed, "OgreStringInterface.h:208; deleting destructor"},
    {1, "setParameter", 0x824AB0B8, Confidence::kInferred, "OgreStringInterface.h:248"},
    {2, "setParameterList", 0x824AB198, Confidence::kInferred, "OgreStringInterface.h:258"},
    {3, "getParameter", 0x82431178, Confidence::kInferred, "OgreStringInterface.h:270"},
    {4, "copyParametersTo", 0x82431200, Confidence::kInferred, "OgreStringInterface.h:301"},
    {5, "preLoadImpl", 0x825BC790, Confidence::kInferred, "OgreResource.h:182"},
    {6, "postLoadImpl", 0x825BC790, Confidence::kInferred, "OgreResource.h:189"},
    {7, "preUnloadImpl", 0x825BC790, Confidence::kInferred, "OgreResource.h:194"},
    {8, "postUnloadImpl", 0x825BC790, Confidence::kInferred, "OgreResource.h:199"},
    {9, "prepareImpl", 0x825BC790, Confidence::kInferred, "OgreResource.h:203"},
    {10, "unprepareImpl", 0x825BC790, Confidence::kInferred, "OgreResource.h:208"},
    {11, "loadImpl", 0x8285C8B0, Confidence::kInferred, "OgreResource.h:212"},
    {12, "unloadImpl", 0x826563A0, Confidence::kConfirmed, "OgreResource.h:216; Texture override: tail-calls vt+0x10C (slot 67 freeInternalResources)"},
    {13, "calculateSize", 0x82655AF8, Confidence::kInferred, "OgreResource.h:218"},
    {14, "prepare", 0x824D7AF0, Confidence::kInferred, "OgreResource.h:259"},
    {15, "load", 0x821C7EA0, Confidence::kConfirmed, "OgreResource.h:271; touch 0x821C7E30 calls it via vt+0x3C"},
    {16, "reload", 0x824D7F00, Confidence::kInferred, "OgreResource.h:278"},
    {17, "isReloadable", 0x824312B0, Confidence::kInferred, "OgreResource.h:282"},
    {18, "isManuallyLoaded", 0x824312D8, Confidence::kInferred, "OgreResource.h:289"},
    {19, "unload", 0x824D7DF8, Confidence::kInferred, "OgreResource.h:297"},
    {20, "getSize", 0x82204278, Confidence::kConfirmed, "OgreResource.h:301; lwz +0x78 (mSize)"},
    {21, "touch", 0x821C7E30, Confidence::kConfirmed, "OgreResource.h:308; D3D9RenderSystem::_setTexture calls it via vt+0x54; RUNIC signature: takes a bool (r4) and only calls load (vt+0x3C) when true, then mCreator(+0x28)->_notifyResourceTouched"},
    {22, "getName", 0x824E5010, Confidence::kConfirmed, "OgreResource.h:312; returns this+0x2C (mName)"},
    {23, "getHandle", 0x824312E0, Confidence::kConfirmed, "OgreResource.h:317; ld +0x68 (mHandle u64)"},
    {24, "isPrepared", 0x824312E8, Confidence::kConfirmed, "OgreResource.h:324; mLoadingState(+0x70)==LOADSTATE_PREPARED(4)"},
    {25, "isLoaded", 0x8219EEF8, Confidence::kConfirmed, "OgreResource.h:332; +0x70==LOADED(2)"},
    {26, "isLoading", 0x821FD6A0, Confidence::kConfirmed, "OgreResource.h:341; +0x70==LOADING(1)"},
    {27, "getLoadingState", 0x821910A8, Confidence::kConfirmed, "OgreResource.h:348; lwz +0x70"},
    {28, "isBackgroundLoaded", 0x82431300, Confidence::kConfirmed, "OgreResource.h:365; lbz +0x74"},
    {29, "setBackgroundLoaded", 0x82431308, Confidence::kConfirmed, "OgreResource.h:375; stb +0x74"},
    {30, "escalateLoading", 0x824D7A98, Confidence::kInferred, "OgreResource.h:386"},
    {31, "addListener", 0x824D7F60, Confidence::kInferred, "OgreResource.h:391"},
    {32, "removeListener", 0x824D7FD8, Confidence::kInferred, "OgreResource.h:396"},
    {33, "getGroup", 0x824E51D8, Confidence::kConfirmed, "OgreResource.h:399; returns this+0x48 (mGroup)"},
    {34, "changeGroupOwnership", 0x824D7D48, Confidence::kInferred, "OgreResource.h:408"},
    {35, "getCreator", 0x82657178, Confidence::kConfirmed, "OgreResource.h:411; lwz +0x28 (mCreator)"},
    {36, "getOrigin", 0x824FF600, Confidence::kConfirmed, "OgreResource.h:418; returns this+0x80 (mOrigin)"},
    {37, "_notifyOrigin", 0x82431310, Confidence::kInferred, "OgreResource.h:420"},
    {38, "getStateCount", 0x823586C8, Confidence::kConfirmed, "OgreResource.h:429; lwz +0xA0 (mStateCount)"},
    {39, "_dirtyState", 0x824D7D38, Confidence::kConfirmed, "OgreResource.h:436; +0xA0 += 1"},
    {40, "_fireLoadingComplete", 0x824D8050, Confidence::kInferred, "OgreResource.h:447"},
    {41, "_firePreparingComplete", 0x824D80C8, Confidence::kInferred, "OgreResource.h:457"},
    {42, "_fireUnloadingComplete", 0x824D8140, Confidence::kInferred, "OgreResource.h:466"},
    {43, "setTextureType", 0x8257A340, Confidence::kConfirmed, "OgreTexture.h:109; stw +0xF8"},
    {44, "getTextureType", 0x821C08B0, Confidence::kConfirmed, "OgreTexture.h:113; lwz +0xF8"},
    {45, "getNumMipmaps", 0x8257A348, Confidence::kConfirmed, "OgreTexture.h:117; lwz +0xC8"},
    {46, "setNumMipmaps", 0x8257A350, Confidence::kConfirmed, "OgreTexture.h:123; stw +0xC8 and +0xC4"},
    {47, "getMipmapsHardwareGenerated", 0x823643B0, Confidence::kConfirmed, "OgreTexture.h:129; lbz +0xCC"},
    {48, "getGamma", 0x8257A360, Confidence::kConfirmed, "OgreTexture.h:133; lfs +0xD0"},
    {49, "setGamma", 0x8257A368, Confidence::kConfirmed, "OgreTexture.h:143; stfs +0xD0"},
    {50, "setHardwareGammaEnabled", 0x8257A370, Confidence::kConfirmed, "OgreTexture.h:163; stb +0xD4"},
    {51, "isHardwareGammaEnabled", 0x8235FE90, Confidence::kConfirmed, "OgreTexture.h:168; lbz +0xD4"},
    {52, "setFSAA", 0x8257A378, Confidence::kConfirmed, "OgreTexture.h:177; stw +0xD8"},
    {53, "getFSAA", 0x8257A398, Confidence::kConfirmed, "OgreTexture.h:182; lwz +0xD8"},
    {54, "getFSAAHint", 0x825A6A78, Confidence::kConfirmed, "OgreTexture.h:186; returns this+0xDC"},
    {55, "getHeight", 0x824E9258, Confidence::kConfirmed, "OgreTexture.h:190; lwz +0xB8"},
    {56, "getWidth", 0x8257A3A0, Confidence::kConfirmed, "OgreTexture.h:194; lwz +0xBC"},
    {57, "getDepth", 0x8257A3A8, Confidence::kConfirmed, "OgreTexture.h:198; lwz +0xC0"},
    {58, "getSrcHeight", 0x82382F60, Confidence::kConfirmed, "OgreTexture.h:202; lwz +0x10C"},
    {59, "getSrcWidth", 0x82500538, Confidence::kConfirmed, "OgreTexture.h:206; lwz +0x108"},
    {60, "getSrcDepth", 0x822BD5E8, Confidence::kConfirmed, "OgreTexture.h:210; lwz +0x110"},
    {61, "setHeight", 0x8257A3B0, Confidence::kConfirmed, "OgreTexture.h:214; stw +0x10C and +0xB8"},
    {62, "setWidth", 0x8257A3C0, Confidence::kConfirmed, "OgreTexture.h:218; stw +0x108 and +0xBC"},
    {63, "setDepth", 0x8257A3D0, Confidence::kConfirmed, "OgreTexture.h:223; stw +0x110 and +0xC0"},
    {64, "getUsage", 0x825004F8, Confidence::kConfirmed, "OgreTexture.h:227; lwz +0x100"},
    {65, "setUsage", 0x8257A3E0, Confidence::kConfirmed, "OgreTexture.h:239; stw +0x100"},
    {66, "createInternalResources", 0x82656300, Confidence::kInferred, "OgreTexture.h:252"},
    {67, "freeInternalResources", 0x82656350, Confidence::kConfirmed, "OgreTexture.h:256; Texture::unloadImpl (slot 12) tail-calls it via vt+0x10C, as upstream"},
    {68, "copyToTexture", 0x826563B0, Confidence::kInferred, "OgreTexture.h:260"},
    {69, "loadImage", 0x826559A0, Confidence::kInferred, "OgreTexture.h:268"},
    {70, "loadRawData", 0x82655910, Confidence::kInferred, "OgreTexture.h:280"},
    {71, "_loadImages", 0x82655B58, Confidence::kInferred, "OgreTexture.h:288"},
    {72, "kRunicSlot72_ReleaseSurfaces", 0x8285C8B0, Confidence::kRunicDifference, "not in 1.7.0; pure in Texture; D3D9Texture impl 0x8257B6E0 releases per-device surfaces when !(mUsage & TU_RENDERTARGET); getFormat and later slots are header index + 1"},
    {73, "getFormat", 0x82378568, Confidence::kConfirmed, "OgreTexture.h:291; lwz +0xFC"},
    {74, "getDesiredFormat", 0x82378560, Confidence::kConfirmed, "OgreTexture.h:297; lwz +0x114"},
    {75, "getSrcFormat", 0x82500518, Confidence::kConfirmed, "OgreTexture.h:305; lwz +0x104"},
    {76, "setFormat", 0x82655A80, Confidence::kConfirmed, "OgreTexture.h:311; stw +0xFC/+0x114/+0x104"},
    {77, "hasAlpha", 0x82655A90, Confidence::kConfirmed, "OgreTexture.h:314; looks +0xFC up in the pixel format table"},
    {78, "setDesiredIntegerBitDepth", 0x82655AB8, Confidence::kConfirmed, "OgreTexture.h:321; sth +0x118"},
    {79, "getDesiredIntegerBitDepth", 0x82655AC0, Confidence::kConfirmed, "OgreTexture.h:325; lhz +0x118"},
    {80, "setDesiredFloatBitDepth", 0x82655AC8, Confidence::kConfirmed, "OgreTexture.h:332; sth +0x11A"},
    {81, "getDesiredFloatBitDepth", 0x82655AD0, Confidence::kConfirmed, "OgreTexture.h:336; lhz +0x11A"},
    {82, "setDesiredBitDepths", 0x82655AD8, Confidence::kConfirmed, "OgreTexture.h:340; sth +0x118 and +0x11A"},
    {83, "setTreatLuminanceAsAlpha", 0x82655AE8, Confidence::kConfirmed, "OgreTexture.h:344; stb +0x11C"},
    {84, "getTreatLuminanceAsAlpha", 0x82655AF0, Confidence::kConfirmed, "OgreTexture.h:348; lbz +0x11C"},
    {85, "getNumFaces", 0x821F8030, Confidence::kInferred, "OgreTexture.h:353"},
    {86, "getBuffer", 0x8285C8B0, Confidence::kConfirmed, "OgreTexture.h:367; pure in Texture (_purecall)"},
    {87, "convertToImage", 0x82656848, Confidence::kInferred, "OgreTexture.h:374"},
    {88, "createInternalResourcesImpl", 0x8285C8B0, Confidence::kConfirmed, "OgreTexture.h:410; pure in Texture"},
    {89, "freeInternalResourcesImpl", 0x8285C8B0, Confidence::kConfirmed, "OgreTexture.h:414; pure in Texture"},
};

}  // namespace ogre
}  // namespace torchlight::guest_abi
