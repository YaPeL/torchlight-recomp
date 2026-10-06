// detile's tiling formula and endian swap against the ReXGlue SDK's (the only place the SDK GPU
// plugin is linked).

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <rex/graphics/pipeline/texture/conversion.h>
#include <rex/graphics/pipeline/texture/util.h>

#include "frontend/detile.h"

int main() {
  namespace fe = torchlight::frontend;
  for (uint32_t log2 : {2u, 3u, 4u})
    for (uint32_t pitch : {32u, 64u, 96u, 256u, 1024u})
      for (int32_t y = 0; y < 128; ++y)
        for (int32_t x = 0; x < int32_t(pitch); ++x)
          if (fe::TiledOffset2D(x, y, pitch, log2) !=
              rex::graphics::texture_util::GetTiledOffset2D(x, y, pitch, log2)) {
            std::fprintf(stderr, "FAIL: tiled offset x=%d y=%d pitch=%u log2=%u\n", x, y, pitch, log2);
            return 1;
          }
  // The SDK's 16in32 case passes the length in bytes as a count of 32-bit words (it touches 4x
  // the bytes), so its buffers are oversized and only the first 64 bytes are compared.
  std::vector<uint8_t> in(256), a(64), b(256);
  for (size_t i = 0; i < in.size(); ++i) in[i] = uint8_t(i * 7 + 3);
  for (uint32_t e = 0; e < 4; ++e) {
    fe::CopySwapBlock(e, a.data(), in.data(), a.size());
    rex::graphics::texture_conversion::CopySwapBlock(rex::graphics::xenos::Endian(e), b.data(),
                                                     in.data(), a.size());
    if (std::memcmp(a.data(), b.data(), a.size()) != 0) {
      std::fprintf(stderr, "FAIL: endian swap %u\n", e);
      return 1;
    }
  }
  std::printf("detile test: ok\n");
  return 0;
}
