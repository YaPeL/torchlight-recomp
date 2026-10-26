#include "hooks/pixel_util.h"

namespace torchlight::hooks {

namespace {

uint32_t ReadBig32(const uint8_t* p) {
  return uint32_t{p[0]} << 24 | uint32_t{p[1]} << 16 | uint32_t{p[2]} << 8 | p[3];
}

}  // namespace

PixelFormatDesc ParsePixelFormat(std::span<const uint8_t, 44> e) {
  PixelFormatDesc d;
  d.elem_bytes = e[4];
  d.flags = ReadBig32(&e[8]);
  for (int c = 0; c < 4; ++c) {
    d.bits[c] = e[17 + c];
    d.masks[c] = ReadBig32(&e[24 + 4 * c]);
    d.shifts[c] = e[40 + c];
  }
  return d;
}

}  // namespace torchlight::hooks
