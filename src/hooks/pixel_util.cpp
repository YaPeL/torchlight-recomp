#include "hooks/pixel_util.h"

namespace torchlight::hooks {

namespace {

uint32_t ReadBig32(const uint8_t* p) {
  return uint32_t{p[0]} << 24 | uint32_t{p[1]} << 16 | uint32_t{p[2]} << 8 | p[3];
}

}  // namespace

PixelFormatDesc ParsePixelFormat(std::span<const uint8_t, 44> e) {
  namespace f = guest_abi::pixel_util::format;
  static_assert(guest_abi::pixel_util::kFormatSize == 44);
  PixelFormatDesc d;
  d.elem_bytes = e[f::kElemBytes.offset];
  d.flags = ReadBig32(&e[f::kFlags.offset]);
  for (uint32_t c = 0; c < 4; ++c) {
    d.bits[c] = e[f::kBits.offset + c];
    d.masks[c] = ReadBig32(&e[f::kMasks.offset + 4 * c]);
    d.shifts[c] = e[f::kShifts.offset + c];
  }
  return d;
}

}  // namespace torchlight::hooks
