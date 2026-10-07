// Xbox 360 D3D objects behind the guest's D3D9 render system, and guest memory addressing.
//
// The guest's D3D9 buffers and textures keep, per device, an Xbox D3D resource whose fetch
// constant tells Xenos where the data lives. These are the offsets the guest's own lock
// functions read. Confidence follows ogre_layout.h.

#pragma once

#include <cstdint>

#include "guest_abi/ogre_layout.h"

namespace torchlight::guest_abi::xbox_d3d {

// [confirmed] Global holding the active IDirect3DDevice9* that keys every per-device resource
// map (D3D9RenderSystem::_render @0x821C42BC, D3D9Texture::getTexture @0x821C9078).
inline constexpr uint32_t kActiveDeviceGlobal = 0x8355A2E4;

// Guest virtual address of a GPU physical address, exactly as the guest's resource lock
// computes it (0x821A6D90 @0x821A70B4..@0x821A70C8). [confirmed]
inline constexpr uint32_t PhysicalToVirtual(uint32_t physical) {
  uint32_t page_bias = ((physical >> 20) + 0x200) & 0x1000;
  return 0xC0000000u + (physical & 0x1FFFFFFFu) + page_bias;
}

namespace vertex_buffer {
// Vertex fetch constant inside the Xbox vertex buffer object (BufferResources::mBuffer).
// [confirmed] lock 0x821A70D8: address = dword0 & ~3 (@0x821A70F4), size in bytes =
// dword1 & 0x03FFFFFC (@0x821A7100). The low two bits of dword1 are the fetch endian swap
// mode [inferred: Xenos vertex fetch constant layout; the lock masks them out].
inline constexpr Field kFetchDword0{0x18, Confidence::kConfirmed};
inline constexpr Field kFetchDword1{0x1C, Confidence::kConfirmed};
inline constexpr uint32_t kAddressMask = 0xFFFFFFFCu;
inline constexpr uint32_t kSizeMask = 0x03FFFFFCu;
inline constexpr uint32_t kEndianMask = 0x3u;
inline constexpr Confidence kEndianConfidence = Confidence::kInferred;
}  // namespace vertex_buffer

namespace index_buffer {
// [confirmed] lock 0x8276BA90: physical address at +0x18 (@0x8276BA9C), size in bytes at +0x1C
// (@0x8276BAA8), both unmasked. There is no endian field: indices are written by the CPU in
// guest (big-endian) order and the swap is chosen by the draw packet.
inline constexpr Field kAddress{0x18, Confidence::kConfirmed};
inline constexpr Field kSize{0x1C, Confidence::kConfirmed};
}  // namespace index_buffer

namespace base_texture {
// Texture fetch constant (6 dwords) inside the Xbox texture object (TextureResources::pBaseTex).
// [confirmed] texture lock 0x82769D78: format = dword1 & 0x3F (@0x82769DE0), base address =
// dword1 & 0xFFFFF000 (@0x82769E2C), mip address = dword5 & 0xFFFFF000 (@0x82769E28).
// [inferred: Xenos texture fetch constant layout] dword1 bits 6..7 are the endian swap mode and
// dword2 holds width-1 (bits 0..12) and height-1 (bits 13..25) for 2D textures.
inline constexpr Field kFetchDword0{0x1C, Confidence::kInferred};
inline constexpr Field kFetchDword1{0x20, Confidence::kConfirmed};
inline constexpr Field kFetchDword2{0x24, Confidence::kInferred};
inline constexpr Field kFetchDword5{0x30, Confidence::kConfirmed};
inline constexpr uint32_t kFetchDwordCount = 6;
inline constexpr uint32_t kFormatMask = 0x3Fu;
inline constexpr uint32_t kAddressMask = 0xFFFFF000u;
inline constexpr uint32_t kEndianShift = 6;
inline constexpr Confidence kEndianConfidence = Confidence::kInferred;

// Bytes per block and block size of the Xenos texture formats the raw dump can size.
// [inferred] Xenos format numbering and sizes (as in Xenia's xenos.h); used only to size raw
// dumps of dynamic textures, never to decode them.
struct FormatBlock {
  uint32_t format;
  uint32_t bytes_per_block;
  uint32_t block_dim;  // 1 for uncompressed, 4 for BCn
};
inline constexpr FormatBlock kFormatBlocks[] = {
    {2, 1, 1},    // k_8
    {3, 2, 1},    // k_1_5_5_5
    {4, 2, 1},    // k_5_6_5
    {5, 2, 1},    // k_6_5_5
    {6, 4, 1},    // k_8_8_8_8
    {7, 4, 1},    // k_2_10_10_10
    {8, 1, 1},    // k_8_A
    {9, 1, 1},    // k_8_B
    {10, 2, 1},   // k_8_8
    {15, 2, 1},   // k_4_4_4_4
    {18, 8, 4},   // k_DXT1
    {19, 16, 4},  // k_DXT2_3
    {20, 16, 4},  // k_DXT4_5
    {24, 4, 1},   // k_16_16
    {26, 8, 1},   // k_16_16_16_16
    {31, 4, 1},   // k_16_16_FLOAT
    {32, 8, 1},   // k_16_16_16_16_FLOAT
    {36, 4, 1},   // k_32_FLOAT
    {37, 8, 1},   // k_32_32_FLOAT
    {38, 16, 1},  // k_32_32_32_32_FLOAT
    {49, 16, 4},  // k_DXN
};
// Xenos tiles in 32x32-block tiles, so a tiled base level is padded to 32 blocks each way.
inline constexpr uint32_t kTileBlocks = 32;
inline constexpr Confidence kFormatBlocksConfidence = Confidence::kInferred;
}  // namespace base_texture

namespace device {
// Guest virtual address of the GPU's progress word, which the GPU advances as it consumes the
// command stream and the wait loops compare against a target. [confirmed] read by
// functions::kGpuProgressWaitStep (lwz r11,11024(r29) @0x827741C4, lwz r8,0(r11) @0x827741D0)
// and by its caller 0x821A5C10 (@0x821A5CB0, @0x821A5CBC). What it counts (a read pointer or a
// fence) is [inferred]; only its changing matters here.
inline constexpr Field kGpuProgressAddress{11024, Confidence::kConfirmed};
}  // namespace device

namespace gpu_wait {
// The state functions::kGpuProgressWaitStep works on (r3). [confirmed] lwz r29,0(r3)
// @0x8277417C: the device; lwz r9,8(r31) @0x827741CC: the progress word's last value seen,
// rewritten when it changes (stw r11,8(r31) @0x827741E8).
inline constexpr Field kDevice{0, Confidence::kConfirmed};
inline constexpr Field kLastProgress{8, Confidence::kConfirmed};
}  // namespace gpu_wait

}  // namespace torchlight::guest_abi::xbox_d3d
