// hooks/pixel_util.h against OGRE 1.7's own PixelUtil::packColour / unpackColour (uint8 versions,
// OgrePixelFormat.cpp) and Bitwise::intRead / intWrite / fixedToFixed (OgreBitwise.h) written here
// as OGRE has them for a big-endian machine, over OGRE 1.7's pixel format table (the same values the
// guest's table holds, guest_abi pixel_util kFormatTable). For each format: every value of each
// channel alone and a large random sample of whole pixels and colours, both directions; any
// difference fails. The comparison with the guest's own recompiled functions runs in the game
// (diagnostics builds, --diag_pixel_util_check: hooks/pixel_util_check.inc).

#include "hooks/pixel_util.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

using torchlight::hooks::PixelFormatDesc;

int failures = 0;

void Fail(const std::string& what) {
  if (++failures <= 20) std::fprintf(stderr, "FAIL: %s\n", what.c_str());
}

// OGRE 1.7's table (OgrePixelFormat.cpp _pixelFormats), the fields the two functions read:
// elemBytes, flags, r/g/b/a bits, masks, shifts.
struct Row {
  const char* name;
  uint8_t elem_bytes;
  uint32_t flags;
  uint8_t bits[4];
  uint32_t masks[4];
  uint8_t shifts[4];
};
constexpr Row kTable[] = {
    {"PF_UNKNOWN", 0, 0x00, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
    {"PF_L8", 1, 0x30, {8, 0, 0, 0}, {0xFF, 0, 0, 0}, {0, 0, 0, 0}},
    {"PF_L16", 2, 0x30, {16, 0, 0, 0}, {0xFFFF, 0, 0, 0}, {0, 0, 0, 0}},
    {"PF_A8", 1, 0x11, {0, 0, 0, 8}, {0, 0, 0, 0xFF}, {0, 0, 0, 0}},
    {"PF_A4L4", 1, 0x31, {4, 0, 0, 4}, {0x0F, 0, 0, 0xF0}, {0, 0, 0, 4}},
    {"PF_BYTE_LA", 2, 0x21, {8, 0, 0, 8}, {0, 0, 0, 0}, {0, 0, 0, 0}},
    {"PF_R5G6B5", 2, 0x10, {5, 6, 5, 0}, {0xF800, 0x07E0, 0x001F, 0}, {11, 5, 0, 0}},
    {"PF_B5G6R5", 2, 0x10, {5, 6, 5, 0}, {0x001F, 0x07E0, 0xF800, 0}, {0, 5, 11, 0}},
    {"PF_A4R4G4B4", 2, 0x11, {4, 4, 4, 4}, {0x0F00, 0x00F0, 0x000F, 0xF000}, {8, 4, 0, 12}},
    {"PF_A1R5G5B5", 2, 0x11, {5, 5, 5, 1}, {0x7C00, 0x03E0, 0x001F, 0x8000}, {10, 5, 0, 15}},
    {"PF_R8G8B8", 3, 0x10, {8, 8, 8, 0}, {0xFF0000, 0x00FF00, 0x0000FF, 0}, {16, 8, 0, 0}},
    {"PF_B8G8R8", 3, 0x10, {8, 8, 8, 0}, {0x0000FF, 0x00FF00, 0xFF0000, 0}, {0, 8, 16, 0}},
    {"PF_A8R8G8B8", 4, 0x11, {8, 8, 8, 8}, {0xFF0000, 0xFF00, 0xFF, 0xFF000000}, {16, 8, 0, 24}},
    {"PF_A8B8G8R8", 4, 0x11, {8, 8, 8, 8}, {0xFF, 0xFF00, 0xFF0000, 0xFF000000}, {0, 8, 16, 24}},
    {"PF_B8G8R8A8", 4, 0x11, {8, 8, 8, 8}, {0xFF00, 0xFF0000, 0xFF000000, 0xFF}, {8, 16, 24, 0}},
    {"PF_A2R10G10B10", 4, 0x11, {10, 10, 10, 2}, {0x3FF00000, 0x000FFC00, 0x000003FF, 0xC0000000}, {20, 10, 0, 30}},
    {"PF_A2B10G10R10", 4, 0x11, {10, 10, 10, 2}, {0x000003FF, 0x000FFC00, 0x3FF00000, 0xC0000000}, {0, 10, 20, 30}},
    {"PF_DXT1", 0, 0x03, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
    {"PF_DXT2", 0, 0x03, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
    {"PF_DXT3", 0, 0x03, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
    {"PF_DXT4", 0, 0x03, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
    {"PF_DXT5", 0, 0x03, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
    {"PF_FLOAT16_RGB", 6, 0x04, {16, 16, 16, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
    {"PF_FLOAT16_RGBA", 8, 0x05, {16, 16, 16, 16}, {0, 0, 0, 0}, {0, 0, 0, 0}},
    {"PF_FLOAT32_RGB", 12, 0x04, {32, 32, 32, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
    {"PF_FLOAT32_RGBA", 16, 0x05, {32, 32, 32, 32}, {0, 0, 0, 0}, {0, 0, 0, 0}},
    {"PF_X8R8G8B8", 4, 0x10, {8, 8, 8, 0}, {0xFF0000, 0xFF00, 0xFF, 0xFF000000}, {16, 8, 0, 24}},
    {"PF_X8B8G8R8", 4, 0x10, {8, 8, 8, 0}, {0xFF, 0xFF00, 0xFF0000, 0xFF000000}, {0, 8, 16, 24}},
    {"PF_R8G8B8A8", 4, 0x11, {8, 8, 8, 8}, {0xFF000000, 0xFF0000, 0xFF00, 0xFF}, {24, 16, 8, 0}},
    {"PF_DEPTH", 4, 0x08, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
    {"PF_SHORT_RGBA", 8, 0x01, {16, 16, 16, 16}, {0, 0, 0, 0}, {0, 0, 0, 0}},
    {"PF_R3G3B2", 1, 0x10, {3, 3, 2, 0}, {0xE0, 0x1C, 0x03, 0}, {5, 2, 0, 0}},
    {"PF_FLOAT16_R", 2, 0x04, {16, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
    {"PF_FLOAT32_R", 4, 0x04, {32, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
    {"PF_SHORT_GR", 4, 0x10, {16, 16, 0, 0}, {0x0000FFFF, 0xFFFF0000, 0, 0}, {0, 16, 0, 0}},
    {"PF_FLOAT16_GR", 4, 0x04, {16, 16, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
    {"PF_FLOAT32_GR", 8, 0x04, {32, 32, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
    {"PF_SHORT_RGB", 6, 0x00, {16, 16, 16, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
    {"PF_PVRTC_RGB2", 0, 0x02, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
    {"PF_PVRTC_RGBA2", 0, 0x03, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
    {"PF_PVRTC_RGB4", 0, 0x02, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
    {"PF_PVRTC_RGBA4", 0, 0x03, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
};
static_assert(std::size(kTable) == 42);

constexpr uint32_t kNativeEndian = 0x10, kLuminance = 0x20, kHasAlpha = 0x01;

// The row as the guest stores it: 44 bytes, big-endian (the name pointer left 0).
std::array<uint8_t, 44> Entry(const Row& r) {
  std::array<uint8_t, 44> e{};
  auto put32 = [&](int at, uint32_t v) {
    for (int i = 0; i < 4; ++i) e[at + i] = static_cast<uint8_t>(v >> (24 - 8 * i));
  };
  e[4] = r.elem_bytes;
  put32(8, r.flags);
  for (int c = 0; c < 4; ++c) {
    e[17 + c] = r.bits[c];
    put32(24 + 4 * c, r.masks[c]);
    e[40 + c] = r.shifts[c];
  }
  return e;
}

// ---- OGRE 1.7, as written there (big-endian target) ----
unsigned int OgreFixedToFixed(uint32_t value, unsigned int n, unsigned int p) {
  if (n > p) {
    value >>= n - p;
  } else if (n < p) {
    if (value == 0)
      value = 0;
    else if (value == (static_cast<unsigned int>(1) << n) - 1)
      value = (1 << p) - 1;
    else
      value = value * (1 << p) / ((1 << n) - 1);
  }
  return value;
}
unsigned int OgreIntRead(const uint8_t* src, int n) {
  switch (n) {
    case 1: return src[0];
    case 2: return uint32_t{src[0]} << 8 | src[1];                         // uint16, big-endian
    case 3: return uint32_t{src[0]} << 16 | uint32_t{src[1]} << 8 | src[2];
    case 4: return uint32_t{src[0]} << 24 | uint32_t{src[1]} << 16 | uint32_t{src[2]} << 8 | src[3];
  }
  return 0;
}
void OgreIntWrite(uint8_t* dest, int n, unsigned int value) {
  switch (n) {
    case 1: dest[0] = static_cast<uint8_t>(value); break;
    case 2: dest[0] = static_cast<uint8_t>(value >> 8); dest[1] = static_cast<uint8_t>(value); break;
    case 3:
      dest[0] = static_cast<uint8_t>((value >> 16) & 0xFF);
      dest[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
      dest[2] = static_cast<uint8_t>(value & 0xFF);
      break;
    case 4:
      for (int i = 0; i < 4; ++i) dest[i] = static_cast<uint8_t>(value >> (24 - 8 * i));
      break;
  }
}
void OgrePack(const Row& d, uint8_t r, uint8_t g, uint8_t b, uint8_t a, uint8_t* dest) {
  const unsigned int value = ((OgreFixedToFixed(r, 8, d.bits[0]) << d.shifts[0]) & d.masks[0]) |
                             ((OgreFixedToFixed(g, 8, d.bits[1]) << d.shifts[1]) & d.masks[1]) |
                             ((OgreFixedToFixed(b, 8, d.bits[2]) << d.shifts[2]) & d.masks[2]) |
                             ((OgreFixedToFixed(a, 8, d.bits[3]) << d.shifts[3]) & d.masks[3]);
  OgreIntWrite(dest, d.elem_bytes, value);
}
void OgreUnpack(const Row& d, const uint8_t* src, uint8_t rgba[4]) {
  const unsigned int value = OgreIntRead(src, d.elem_bytes);
  if (d.flags & kLuminance) {
    rgba[0] = rgba[1] = rgba[2] =
        static_cast<uint8_t>(OgreFixedToFixed((value & d.masks[0]) >> d.shifts[0], d.bits[0], 8));
  } else {
    for (int c = 0; c < 3; ++c)
      rgba[c] = static_cast<uint8_t>(OgreFixedToFixed((value & d.masks[c]) >> d.shifts[c], d.bits[c], 8));
  }
  rgba[3] = (d.flags & kHasAlpha)
                ? static_cast<uint8_t>(OgreFixedToFixed((value & d.masks[3]) >> d.shifts[3], d.bits[3], 8))
                : 255;
}
// ---- end of OGRE ----

uint64_t seed = 0x9E3779B97F4A7C15ull;
uint32_t Random() {
  seed ^= seed << 13;
  seed ^= seed >> 7;
  seed ^= seed << 17;
  return static_cast<uint32_t>(seed);
}

void CheckUnpack(const Row& row, const PixelFormatDesc& desc, uint32_t raw, uint64_t& checks) {
  std::array<uint8_t, 4> pixel;
  for (int i = 0; i < 4; ++i) pixel[i] = static_cast<uint8_t>(raw >> (24 - 8 * i));
  if (row.elem_bytes >= 1 && row.elem_bytes <= 4) {  // the pixel's bytes first, as in memory
    const uint32_t v = raw & (row.elem_bytes == 4 ? 0xFFFFFFFFu : (1u << (8 * row.elem_bytes)) - 1);
    for (int i = 0; i < row.elem_bytes; ++i) pixel[i] = static_cast<uint8_t>(v >> (8 * (row.elem_bytes - 1 - i)));
  }
  uint8_t want[4];
  OgreUnpack(row, pixel.data(), want);
  std::array<uint8_t, 4> got{};
  if (!torchlight::hooks::UnpackColour8(desc, pixel, got)) {
    Fail(std::string(row.name) + ": unpack not handled");
    return;
  }
  ++checks;
  if (std::memcmp(want, got.data(), 4) != 0)
    Fail(std::string(row.name) + " unpack " + std::to_string(raw) + ": " + std::to_string(want[0]) + "," +
         std::to_string(want[1]) + "," + std::to_string(want[2]) + "," + std::to_string(want[3]) + " vs " +
         std::to_string(got[0]) + "," + std::to_string(got[1]) + "," + std::to_string(got[2]) + "," +
         std::to_string(got[3]));
}

void CheckPack(const Row& row, const PixelFormatDesc& desc, std::array<uint8_t, 4> rgba, uint64_t& checks) {
  const uint32_t fill = Random();
  std::array<uint8_t, 4> want, got;
  for (int i = 0; i < 4; ++i) want[i] = got[i] = static_cast<uint8_t>(fill >> (8 * i));
  OgrePack(row, rgba[0], rgba[1], rgba[2], rgba[3], want.data());
  std::array<uint8_t, 4> out = got;
  const int n = torchlight::hooks::PackColour8(desc, rgba, out);
  if (n < 0) {
    Fail(std::string(row.name) + ": pack not handled");
    return;
  }
  for (int i = 0; i < n; ++i) got[i] = out[i];
  ++checks;
  if (want != got) Fail(std::string(row.name) + " pack " + std::to_string(rgba[0]) + "," + std::to_string(rgba[1]) +
                        "," + std::to_string(rgba[2]) + "," + std::to_string(rgba[3]));
}

}  // namespace

int main() {
  // fixedToFixed on its own, every n, p up to 16 and every value of n bits.
  for (unsigned n = 0; n <= 16; ++n)
    for (unsigned p = 0; p <= 16; ++p)
      for (uint32_t v = 0; v < (1u << n); ++v) {
        if (n < p && n == 0 && v) continue;  // a division by zero in both (never reached)
        if (OgreFixedToFixed(v, n, p) != torchlight::hooks::FixedToFixed(v, n, p))
          Fail("fixedToFixed " + std::to_string(v) + " " + std::to_string(n) + " " + std::to_string(p));
      }
  uint64_t checks = 0;
  int handled = 0;
  for (const Row& row : kTable) {
    const PixelFormatDesc desc = torchlight::hooks::ParsePixelFormat(Entry(row));
    if (desc.elem_bytes != row.elem_bytes || desc.flags != row.flags) Fail(std::string(row.name) + ": parse");
    const bool native = row.flags & kNativeEndian;
    std::array<uint8_t, 4> pixel{}, rgba{};
    std::array<uint8_t, 4> out{};
    if (!native) {
      // Not handled: the hooks run the guest's own function.
      if (torchlight::hooks::UnpackColour8(desc, pixel, rgba) || torchlight::hooks::PackColour8(desc, rgba, out) >= 0)
        Fail(std::string(row.name) + ": a non-native format handled");
      continue;
    }
    ++handled;
    for (int c = 0; c < 4; ++c) {
      if (!row.bits[c] || !row.masks[c]) continue;
      for (uint32_t v = 0; v < (1u << row.bits[c]); ++v)
        CheckUnpack(row, desc, (Random() & ~row.masks[c]) | ((v << row.shifts[c]) & row.masks[c]), checks);
    }
    for (int i = 0; i < 1000000; ++i) CheckUnpack(row, desc, Random(), checks);
    for (int c = 0; c < 4; ++c)
      for (uint32_t v = 0; v < 256; ++v) {
        std::array<uint8_t, 4> in{static_cast<uint8_t>(Random()), static_cast<uint8_t>(Random()),
                                  static_cast<uint8_t>(Random()), static_cast<uint8_t>(Random())};
        in[c] = static_cast<uint8_t>(v);
        CheckPack(row, desc, in, checks);
      }
    for (int i = 0; i < 1000000; ++i) {
      const uint32_t r = Random();
      CheckPack(row, desc,
                {static_cast<uint8_t>(r), static_cast<uint8_t>(r >> 8), static_cast<uint8_t>(r >> 16),
                 static_cast<uint8_t>(r >> 24)},
                checks);
    }
  }
  std::printf("pixel_util_test: %d native formats, %llu comparisons, %d failures\n", handled,
              static_cast<unsigned long long>(checks), failures);
  return failures ? 1 : 0;
}
