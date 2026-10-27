// The guest OGRE's 8-bit colour pack and unpack (guest_abi/pixel_util.h kPackColour8,
// kUnpackColour8) on the host, for the formats the guest packs with integer masks and shifts
// (kNativeEndian). The same steps as the recompiled code, in the same order and with the same
// integer arithmetic, so every input gives the same bytes; the other formats (floats, compressed,
// PF_BYTE_LA, PF_SHORT_RGB(A)) answer "not handled" and the hooks run the guest's own function.
// No guest memory here: the description and the pixel come in as bytes, in the guest's byte order.

#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "guest_abi/pixel_util.h"

namespace torchlight::hooks {

// One entry of the guest's pixel format table (guest_abi pixel_util::format).
struct PixelFormatDesc {
  uint8_t elem_bytes = 0;
  uint32_t flags = 0;
  std::array<uint8_t, 4> bits{};    // r, g, b, a
  std::array<uint32_t, 4> masks{};  // r, g, b, a
  std::array<uint8_t, 4> shifts{};  // r, g, b, a
};

// An entry from its 44 bytes as the guest stores them (big-endian).
PixelFormatDesc ParsePixelFormat(std::span<const uint8_t, 44> entry);

namespace pixel_detail {
inline constexpr uint32_t kHasAlpha = guest_abi::pixel_util::kHasAlpha;
inline constexpr uint32_t kNativeEndian = guest_abi::pixel_util::kNativeEndian;
inline constexpr uint32_t kLuminance = guest_abi::pixel_util::kLuminance;
// PowerPC slw and srw as the recompiled code does them: the amount's low 6 bits; with bit 5 set the
// result is 0, else a shift by the low 5 bits.
inline uint32_t Slw(uint32_t value, uint32_t shift) { return (shift & 0x20) ? 0 : value << (shift & 0x1F); }
inline uint32_t Srw(uint32_t value, uint32_t shift) { return (shift & 0x20) ? 0 : value >> (shift & 0x1F); }
}  // namespace pixel_detail

// Bitwise::fixedToFixed as the guest runs it (sub_824732C8), including its unsigned 32-bit
// arithmetic. n == 0 with a non-zero value divides by zero there (a trap); callers never reach it
// (a zero mask gives a zero value) and this returns 0 for it.
inline uint32_t FixedToFixed(uint32_t value, uint32_t n, uint32_t p) {
  using namespace pixel_detail;
  if (n > p) return Srw(value, n - p);
  if (n == p || value == 0) return value;
  const uint32_t max_n = Slw(1, n) - 1;
  if (value == max_n) return Slw(1, p) - 1;
  if (max_n == 0) return 0;              // the guest traps here (twllei); never reached
  return (Slw(1, p) * value) / max_n;    // mullw and divwu: 32-bit unsigned
}

// The guest's read of one pixel by element size (sub_82473388): 1 to 4 big-endian bytes, else 0.
inline uint32_t ReadPixel(const uint8_t* p, uint8_t elem_bytes) {
  switch (elem_bytes) {
    case 1: return p[0];
    case 2: return uint32_t{p[0]} << 8 | p[1];
    case 3: return uint32_t{p[0]} << 16 | uint32_t{p[1]} << 8 | p[2];
    case 4: return uint32_t{p[0]} << 24 | uint32_t{p[1]} << 16 | uint32_t{p[2]} << 8 | p[3];
    default: return 0;
  }
}

// unpackColour to 8 bits: rgba from the pixel's first elem_bytes bytes. False (nothing written)
// for a format without kNativeEndian.
inline bool UnpackColour8(const PixelFormatDesc& d, std::span<const uint8_t, 4> pixel, std::array<uint8_t, 4>& rgba) {
  using namespace pixel_detail;
  if (!(d.flags & kNativeEndian)) return false;
  const uint32_t value = ReadPixel(pixel.data(), d.elem_bytes);
  auto channel = [&](int c) {
    return static_cast<uint8_t>(FixedToFixed(Srw(value & d.masks[c], d.shifts[c]), d.bits[c], 8));
  };
  if (d.flags & kLuminance) {
    rgba[0] = rgba[1] = rgba[2] = channel(0);
  } else {
    rgba[0] = channel(0);
    rgba[1] = channel(1);
    rgba[2] = channel(2);
  }
  rgba[3] = (d.flags & kHasAlpha) ? channel(3) : 255;
  return true;
}

// packColour from 8 bits: the pixel's bytes, written to out[0..elem_bytes) (elem_bytes 1 to 4;
// any other size writes nothing, as the guest). Returns how many bytes it wrote, or -1 for a format
// without kNativeEndian.
inline int PackColour8(const PixelFormatDesc& d, std::array<uint8_t, 4> rgba, std::array<uint8_t, 4>& out) {
  using namespace pixel_detail;
  if (!(d.flags & kNativeEndian)) return -1;
  uint32_t value = 0;
  for (int c = 0; c < 4; ++c) value |= Slw(FixedToFixed(rgba[c], 8, d.bits[c]), d.shifts[c]) & d.masks[c];
  switch (d.elem_bytes) {
    case 1:
      out[0] = static_cast<uint8_t>(value);
      return 1;
    case 2:
      out[0] = static_cast<uint8_t>(value >> 8);
      out[1] = static_cast<uint8_t>(value);
      return 2;
    case 3:
      out[0] = static_cast<uint8_t>(value >> 16);
      out[1] = static_cast<uint8_t>(value >> 8);
      out[2] = static_cast<uint8_t>(value);
      return 3;
    case 4:
      out[0] = static_cast<uint8_t>(value >> 24);
      out[1] = static_cast<uint8_t>(value >> 16);
      out[2] = static_cast<uint8_t>(value >> 8);
      out[3] = static_cast<uint8_t>(value);
      return 4;
    default:
      return 0;
  }
}

}  // namespace torchlight::hooks
