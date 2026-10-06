#include "frontend/detile.h"

#include <cstring>

#include "backend/backend_api.h"

namespace torchlight::frontend {

namespace {

struct XenosFormat {
  uint32_t xenos;       // fetch constant dword1 bits 0..5
  uint32_t linear;      // tl_linear_format
  uint32_t block_dim;   // 1 or 4
  uint32_t bytes_per_block;
  uint32_t bytes_per_block_log2;
};
// Formats the first milestone handles (Xenos numbering as in rex/graphics/xenos.h).
constexpr XenosFormat kFormats[] = {
    {6, TL_LINEAR_BGRA8, 1, 4, 2},   // k_8_8_8_8 (D3D A8R8G8B8)
    {18, TL_LINEAR_DXT1, 4, 8, 3},   // k_DXT1
    {19, TL_LINEAR_DXT3, 4, 16, 4},  // k_DXT2_3
    {20, TL_LINEAR_DXT5, 4, 16, 4},  // k_DXT4_5
};

uint32_t Align(uint32_t v, uint32_t a) { return (v + a - 1) / a * a; }

}  // namespace

int32_t TiledOffset2D(int32_t x, int32_t y, uint32_t pitch, uint32_t bytes_per_block_log2) {
  // As texture_util::GetTiledOffset2D (from UModel, UnTexture.cpp#L489).
  pitch = Align(pitch, 32);
  int32_t macro = ((x >> 5) + (y >> 5) * int32_t(pitch >> 5)) << (bytes_per_block_log2 + 7);
  int32_t micro = ((x & 7) + ((y & 0xE) << 2)) << bytes_per_block_log2;
  int32_t offset = macro + ((micro & ~0xF) << 1) + (micro & 0xF) + ((y & 1) << 4);
  return ((offset & ~0x1FF) << 3) + ((y & 16) << 7) + ((offset & 0x1C0) << 2) +
         (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) + (offset & 0x3F);
}

void CopySwapBlock(uint32_t endian, uint8_t* out, const uint8_t* in, size_t length) {
  switch (endian & 3) {
    case 1:  // 8in16
      for (size_t i = 0; i + 1 < length; i += 2) {
        out[i] = in[i + 1];
        out[i + 1] = in[i];
      }
      break;
    case 2:  // 8in32
      for (size_t i = 0; i + 3 < length; i += 4) {
        out[i] = in[i + 3];
        out[i + 1] = in[i + 2];
        out[i + 2] = in[i + 1];
        out[i + 3] = in[i];
      }
      break;
    case 3:  // 16in32
      for (size_t i = 0; i + 3 < length; i += 4) {
        out[i] = in[i + 2];
        out[i + 1] = in[i + 3];
        out[i + 2] = in[i];
        out[i + 3] = in[i + 1];
      }
      break;
    default:
      std::memcpy(out, in, length);
      break;
  }
}

bool DetileTexture(const commands::TextureDesc& desc, const commands::Blob& blob,
                   LinearTexture& out, std::string& reason) {
  uint32_t xenos = desc.fetch_constant[1] & 0x3F;
  const XenosFormat* f = nullptr;
  for (const auto& candidate : kFormats) {
    if (candidate.xenos == xenos) f = &candidate;
  }
  if (f == nullptr) {
    reason = "xenos format " + std::to_string(xenos) + " not handled";
    return false;
  }
  uint32_t blocks_w = (desc.width + f->block_dim - 1) / f->block_dim;
  uint32_t blocks_h = (desc.height + f->block_dim - 1) / f->block_dim;
  uint32_t pitch = Align(blocks_w, 32);
  out.width = desc.width;
  out.height = desc.height;
  out.format = f->linear;
  out.data.assign(size_t(blocks_w) * blocks_h * f->bytes_per_block, 0);
  uint32_t endian = blob.endian_raw & 3u;
  for (uint32_t y = 0; y < blocks_h; ++y) {
    for (uint32_t x = 0; x < blocks_w; ++x) {
      int32_t offset = TiledOffset2D(int32_t(x), int32_t(y), pitch, f->bytes_per_block_log2);
      if (offset < 0 || size_t(offset) + f->bytes_per_block > blob.bytes.size()) {
        reason = "tiled offset outside the captured content";
        return false;
      }
      CopySwapBlock(endian, &out.data[(size_t(y) * blocks_w + x) * f->bytes_per_block],
                    &blob.bytes[size_t(offset)], f->bytes_per_block);
    }
  }
  return true;
}

}  // namespace torchlight::frontend
