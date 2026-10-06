// Xenos texture content (tiled, guest byte order) -> linear host texture.
//
// The backend only ever receives linear textures. The tiling formula and the endian swap are the
// ones the ReXGlue SDK uses (texture_util::GetTiledOffset2D, texture_conversion::CopySwapBlock),
// kept here instead of linking its GPU plugin: the SDK loads that plugin at runtime, and linking it
// too would put two copies of it in the process (duplicated cvar registration). detile_test checks
// them against the SDK.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "commands/types.h"

namespace torchlight::frontend {

struct LinearTexture {
  uint32_t width = 0, height = 0;
  uint32_t format = 0;  // tl_linear_format
  std::vector<uint8_t> data;
};

// Base level of a dynamic texture captured raw. Returns false with `reason` set when the Xenos
// format is not handled yet.
bool DetileTexture(const commands::TextureDesc& desc, const commands::Blob& blob,
                   LinearTexture& out, std::string& reason);

// Byte offset of block (x, y) in a 2D Xenos tiled surface whose row pitch is `pitch` blocks.
int32_t TiledOffset2D(int32_t x, int32_t y, uint32_t pitch, uint32_t bytes_per_block_log2);
// Copies `length` bytes undoing the fetch endian swap `endian` (0 none, 1 8in16, 2 8in32,
// 3 16in32).
void CopySwapBlock(uint32_t endian, uint8_t* output, const uint8_t* input, size_t length);

}  // namespace torchlight::frontend
