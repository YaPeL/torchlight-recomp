// The wardrobe build's blend loop (guest_abi wardrobe::blend, docs/wardrobe-texture.md "The blend,
// exactly") on the host: one call per item image, the whole rows x columns block, with the
// recompiled loop's arithmetic expression by expression (each fmuls/fsubs a double product rounded
// to single, each fmadds std::fma rounded to single, fctidz and its lowest byte), the colour
// pack/unpack of hooks/pixel_util.h, the pixels read and written in the guest's order, and the
// loop's stack temporaries left as its last iteration leaves them. Compiled with
// -ffp-contract=off: no operation may be fused beyond what the recompiled code fuses.
//
// Memory is the caller's: Load8/Store8 at a guest address, BeforeBlend() where the recompiled
// code's blend path sets the host's floating-point mode (fpscr.disableFlushMode()), Packed(dest)
// after each pixel written into the base.

#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

#include "guest_abi/wardrobe.h"
#include "hooks/pixel_util.h"

namespace torchlight::hooks {

struct WardrobeBlendLayer {
  uint32_t sp = 0;      // the build's r1
  uint32_t item = 0;    // the item image's first pixel (r29)
  uint32_t base = 0;    // the base's first pixel (r31)
  uint32_t rows = 0;    // r26, non-zero
  uint32_t columns = 0; // [r1+140], non-zero
  PixelFormatDesc item_format;  // r27's, kNativeEndian
  PixelFormatDesc base_format;  // r19's, kNativeEndian
  double alpha_scale = 0;  // f31: the float at kAlphaScale
  double one = 0;          // f30: the float at kOne
};

namespace blend_detail {

// frsp, and the single-precision result of fmuls, fsubs, fmadds: the double rounded to float.
inline double Single(double x) { return static_cast<double>(static_cast<float>(x)); }

// fctidz as the recompiled code does it: NaN gives INT64_MIN, at or above 2^63 INT64_MAX, else
// cvttsd2si (INT64_MIN below -2^63).
inline int64_t Fctidz(double x) {
  if (std::isnan(x)) return std::numeric_limits<int64_t>::min();
  if (x >= static_cast<double>(std::numeric_limits<int64_t>::max())) return std::numeric_limits<int64_t>::max();
  if (x < -9223372036854775808.0) return std::numeric_limits<int64_t>::min();
  return static_cast<int64_t>(x);
}

template <class Memory>
void Store64(Memory& m, uint32_t address, uint64_t value) {
  for (int i = 0; i < 8; ++i) m.Store8(address + i, static_cast<uint8_t>(value >> (56 - 8 * i)));
}

template <class Memory>
std::array<uint8_t, 4> Unpack(Memory& m, const PixelFormatDesc& format, uint32_t src) {
  std::array<uint8_t, 4> pixel{};
  for (uint32_t i = 0; i < format.elem_bytes && i < 4; ++i) pixel[i] = m.Load8(src + i);
  std::array<uint8_t, 4> rgba{};
  UnpackColour8(format, pixel, rgba);
  return rgba;
}

template <class Memory>
void Pack(Memory& m, const PixelFormatDesc& format, std::array<uint8_t, 4> rgba, uint32_t dest) {
  std::array<uint8_t, 4> out{};
  const int n = PackColour8(format, rgba, out);
  for (int i = 0; i < n; ++i) m.Store8(dest + i, out[i]);
  m.Packed(dest);
}

}  // namespace blend_detail

// The loop for one item image. Both formats must have kNativeEndian (the caller falls back to the
// recompiled loop otherwise).
template <class Memory>
void BlendWardrobeLayer(Memory& m, const WardrobeBlendLayer& layer) {
  using namespace blend_detail;
  namespace st = guest_abi::wardrobe::blend::stack;
  // The temporaries' last values: the item's four bytes (or, after a blend, the blended channels
  // and r17), and those only a blend writes.
  std::array<uint8_t, 4> item_slots{};  // r, g, b, a slots
  bool blended = false;
  std::array<uint8_t, 4> base_rgba{};
  uint64_t item_d[4]{}, base_d[3]{};  // r, g, b, a; r, g, b
  int64_t truncated = 0;
  uint32_t item = layer.item, base = layer.base;
  for (uint32_t row = 0; row < layer.rows; ++row) {
    for (uint32_t column = 0; column < layer.columns; ++column, item += 4, base += 4) {
      const std::array<uint8_t, 4> a = Unpack(m, layer.item_format, item);
      if (a[3] >= 255) {
        item_slots = a;
        Pack(m, layer.base_format, a, base);
        continue;
      }
      if (!blended) m.BeforeBlend();
      blended = true;
      base_rgba = Unpack(m, layer.base_format, base);
      const std::array<uint8_t, 4> b = base_rgba;
      // The recompiled code's order of operations (@0x822DD288..0x822DD36C).
      const double alpha_in = Single(static_cast<double>(static_cast<int64_t>(a[3])));  // fcfid, frsp
      const double alpha = Single(alpha_in * layer.alpha_scale);                         // fmuls f4
      const double ag = Single(static_cast<double>(a[1])), ab = Single(static_cast<double>(a[2]));
      const double ar = Single(static_cast<double>(a[0]));
      const double bg = Single(static_cast<double>(b[1])), br = Single(static_cast<double>(b[0]));
      const double bb = Single(static_cast<double>(b[2]));
      const double inv = Single(layer.one - alpha);  // fsubs f10
      const double ag_a = Single(ag * alpha), ab_a = Single(ab * alpha), ar_a = Single(ar * alpha);
      const double green = Single(std::fma(bg, inv, ag_a));  // fmadds f2
      const double blue = Single(std::fma(bb, inv, ab_a));   // fmadds f0
      const double red = Single(std::fma(br, inv, ar_a));    // fmadds f12
      const uint8_t g8 = static_cast<uint8_t>(Fctidz(green));
      const uint8_t b8 = static_cast<uint8_t>(Fctidz(blue));
      truncated = Fctidz(red);
      const uint8_t r8 = static_cast<uint8_t>(truncated);
      item_d[0] = a[0], item_d[1] = a[1], item_d[2] = a[2], item_d[3] = a[3];
      base_d[0] = b[0], base_d[1] = b[1], base_d[2] = b[2];
      item_slots = {r8, g8, b8, guest_abi::wardrobe::blend::kBlendedItemAlpha};
      Pack(m, layer.base_format, {r8, g8, b8, 255}, base);
    }
  }
  const uint32_t sp = layer.sp;
  m.Store8(sp + st::kItemR, item_slots[0]);
  m.Store8(sp + st::kItemG, item_slots[1]);
  m.Store8(sp + st::kItemB, item_slots[2]);
  m.Store8(sp + st::kItemA, item_slots[3]);
  if (!blended) return;
  m.Store8(sp + st::kBaseR, base_rgba[0]);
  m.Store8(sp + st::kBaseG, base_rgba[1]);
  m.Store8(sp + st::kBaseB, base_rgba[2]);
  m.Store8(sp + st::kBaseA, base_rgba[3]);
  Store64(m, sp + st::kItemRDouble, item_d[0]);
  Store64(m, sp + st::kItemGDouble, item_d[1]);
  Store64(m, sp + st::kItemBDouble, item_d[2]);
  Store64(m, sp + st::kItemADouble, item_d[3]);
  Store64(m, sp + st::kBaseRDouble, base_d[0]);
  Store64(m, sp + st::kBaseGDouble, base_d[1]);
  Store64(m, sp + st::kBaseBDouble, base_d[2]);
  Store64(m, sp + st::kTruncated, static_cast<uint64_t>(truncated));
}

}  // namespace torchlight::hooks
