// hooks/wardrobe_blend.h against the recompiled loop it replaces, written here instruction by
// instruction as the generated code does it (sub_822DC6B0, 0x822DD220..0x822DD398: the same
// expressions on doubles, the same stores to the stack, unpackColour/packColour through their
// registers) over a small memory with big-endian pixels. For every pair of ten native-endian
// formats (the loop steps 4 bytes whatever their size), random images (every alpha value present, the opaque
// one often), several sizes: the base's bytes, the whole stack frame and r26 must be identical; any
// difference fails. The comparison with the game's own loop runs in the game (diagnostics builds,
// --diag_wardrobe_blend_check).

#include "hooks/wardrobe_blend.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "guest_abi/pixel_util.h"

namespace {

using torchlight::hooks::PixelFormatDesc;

int failures = 0;
void Fail(const std::string& what) {
  if (++failures <= 20) std::fprintf(stderr, "FAIL: %s\n", what.c_str());
}

uint64_t seed = 0x2545F4914F6CDD1Dull;
uint32_t Random() {
  seed ^= seed << 13;
  seed ^= seed >> 7;
  seed ^= seed << 17;
  return static_cast<uint32_t>(seed);
}

// A memory of guest addresses 0..size.
struct TestMemory {
  std::vector<uint8_t> bytes;
  int blends = 0;
  uint8_t Load8(uint32_t a) const { return bytes.at(a); }
  void Store8(uint32_t a, uint8_t v) { bytes.at(a) = v; }
  void BeforeBlend() { ++blends; }
  void Packed(uint32_t) {}
  uint64_t Load64(uint32_t a) const {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v = v << 8 | bytes.at(a + i);
    return v;
  }
};

// ---- the recompiled loop ----
struct Reg {
  uint64_t u64 = 0;
  double f64 = 0;
  uint32_t u32() const { return static_cast<uint32_t>(u64); }
};
struct Regs {
  Reg r[32];
  Reg f[32];
};

// unpackColour(r3 r*, r4 g*, r5 b*, r6 a*, r7 format, r8 src) and packColour(r3..r6, r7, r8 dest):
// hooks/pixel_util.h, which pixel_util_test proves equal to OGRE's and the game to the guest's.
void GuestUnpack(Regs& x, TestMemory& m, const PixelFormatDesc* formats) {
  const PixelFormatDesc& d = formats[x.r[7].u32()];
  std::array<uint8_t, 4> pixel{};
  for (uint32_t i = 0; i < d.elem_bytes && i < 4; ++i) pixel[i] = m.Load8(x.r[8].u32() + i);
  std::array<uint8_t, 4> rgba{};
  torchlight::hooks::UnpackColour8(d, pixel, rgba);
  m.Store8(x.r[3].u32(), rgba[0]);
  m.Store8(x.r[4].u32(), rgba[1]);
  m.Store8(x.r[5].u32(), rgba[2]);
  m.Store8(x.r[6].u32(), rgba[3]);
}
void GuestPack(Regs& x, TestMemory& m, const PixelFormatDesc* formats) {
  const PixelFormatDesc& d = formats[x.r[7].u32()];
  std::array<uint8_t, 4> out{};
  const int n = torchlight::hooks::PackColour8(
      d,
      {static_cast<uint8_t>(x.r[3].u64), static_cast<uint8_t>(x.r[4].u64), static_cast<uint8_t>(x.r[5].u64),
       static_cast<uint8_t>(x.r[6].u64)},
      out);
  for (int i = 0; i < n; ++i) m.Store8(x.r[8].u32() + i, out[i]);
}

double Single(double v) { return static_cast<double>(static_cast<float>(v)); }
int64_t Fctidz(double v) {
  // The generated expression: isnan ? INT64_MIN : v >= LLONG_MAX ? LLONG_MAX : cvttsd2si.
  if (std::isnan(v)) return std::numeric_limits<int64_t>::min();
  if (v >= static_cast<double>(std::numeric_limits<int64_t>::max())) return std::numeric_limits<int64_t>::max();
  if (v < -9223372036854775808.0) return std::numeric_limits<int64_t>::min();
  return static_cast<int64_t>(v);
}
int64_t AsS64(const Reg& reg) { return static_cast<int64_t>(reg.u64); }

void Std(TestMemory& m, uint32_t a, uint64_t v) {
  for (int i = 0; i < 8; ++i) m.Store8(a + i, static_cast<uint8_t>(v >> (56 - 8 * i)));
}

// From loc_822DD220 with r26 set (rotlwi r26,r10,0), to the exit.
void GuestLoop(Regs& x, TestMemory& m, const PixelFormatDesc* formats) {
  Reg* r = x.r;
  Reg* f = x.f;
  const uint32_t sp = r[1].u32();
  auto lbz = [&](uint32_t off) { return uint64_t{m.Load8(sp + off)}; };
  auto lfd = [&](uint32_t off) {  // the bits of a stored integer, read as a double
    Reg v;
    v.u64 = m.Load64(sp + off);
    return v;
  };
loc_822DD220:
  r[11].u64 = 0;
  for (int i = 0; i < 4; ++i) r[11].u64 = r[11].u64 << 8 | m.Load8(sp + 140 + i);  // lwz r11,140(r1)
  if (r[11].u32() == 0) goto loc_822DD394;
  r[28].u64 = r[11].u32();  // rotlwi r28,r11,0
loc_822DD230:
  r[8].u64 = r[29].u64;
  r[7].u64 = r[27].u64;
  r[6].u64 = sp + 131;
  r[5].u64 = sp + 128;
  r[4].u64 = sp + 130;
  r[3].u64 = sp + 129;
  GuestUnpack(x, m, formats);  // bl 0x82474560
  r[6].u64 = lbz(131);
  r[8].u64 = r[31].u64;
  r[30].u64 = r[6].u64;
  r[7].u64 = r[19].u64;
  if (r[6].u32() < 255) goto loc_822DD274;
  r[5].u64 = lbz(128);
  r[4].u64 = lbz(130);
  r[3].u64 = lbz(129);
  goto loc_822DD380;
loc_822DD274: {
  r[6].u64 = sp + 288;
  r[5].u64 = sp + 144;
  r[4].u64 = sp + 145;
  r[3].u64 = sp + 152;
  GuestUnpack(x, m, formats);  // bl 0x82474560
  r[4].u64 = lbz(130);
  r[6].u64 = lbz(128);
  r[10].u64 = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(r[30].u32())));  // extsw
  r[5].u64 = r[4].u64;
  r[11].u64 = lbz(129);
  r[7].u64 = lbz(145);
  Std(m, sp + 384, r[5].u64);
  f[8] = lfd(384);
  Std(m, sp + 336, r[10].u64);
  f[0] = lfd(336);
  Std(m, sp + 592, r[6].u64);
  f[12] = lfd(592);
  Std(m, sp + 304, r[11].u64);
  f[7] = lfd(304);
  Std(m, sp + 600, r[7].u64);
  f[10] = lfd(600);
  f[13].f64 = static_cast<double>(AsS64(f[0]));  // fcfid
  r[8].u64 = lbz(152);
  r[6].u64 = lbz(144);
  f[11].f64 = Single(f[13].f64);
  f[6].f64 = static_cast<double>(AsS64(f[8]));
  r[4].u64 = r[11].u64;
  f[5].f64 = static_cast<double>(AsS64(f[12]));
  r[11].u64 = r[8].u64;
  f[13].f64 = static_cast<double>(AsS64(f[7]));
  m.Store8(sp + 131, static_cast<uint8_t>(r[17].u64));
  f[3].f64 = static_cast<double>(AsS64(f[10]));
  Std(m, sp + 296, r[11].u64);
  Std(m, sp + 528, r[6].u64);
  f[9] = lfd(528);
  f[1] = lfd(296);
  f[12].f64 = static_cast<double>(AsS64(f[1]));
  f[4].f64 = Single(f[11].f64 * f[31].f64);  // fmuls f4,f11,f31
  r[8].u64 = r[31].u64;
  f[0].f64 = Single(f[6].f64);
  r[7].u64 = r[19].u64;
  f[2].f64 = static_cast<double>(AsS64(f[9]));
  r[6].u64 = 255;
  f[11].f64 = Single(f[5].f64);
  f[6].f64 = Single(f[13].f64);
  f[9].f64 = Single(f[3].f64);
  f[5].f64 = Single(f[12].f64);
  f[10].f64 = Single(f[30].f64 - f[4].f64);  // fsubs f10,f30,f4
  f[7].f64 = Single(f[0].f64 * f[4].f64);
  f[8].f64 = Single(f[2].f64);
  f[3].f64 = Single(f[11].f64 * f[4].f64);
  f[1].f64 = Single(f[6].f64 * f[4].f64);
  f[2].f64 = Single(std::fma(f[9].f64, f[10].f64, f[7].f64));  // fmadds f2,f9,f10,f7
  f[0].f64 = Single(std::fma(f[8].f64, f[10].f64, f[3].f64));
  f[12].f64 = Single(std::fma(f[5].f64, f[10].f64, f[1].f64));
  f[13].u64 = static_cast<uint64_t>(Fctidz(f[2].f64));
  Std(m, sp + 192, f[13].u64);
  r[4].u64 = lbz(199);
  f[11].u64 = static_cast<uint64_t>(Fctidz(f[0].f64));
  Std(m, sp + 192, f[11].u64);
  r[5].u64 = lbz(199);
  f[10].u64 = static_cast<uint64_t>(Fctidz(f[12].f64));
  Std(m, sp + 192, f[10].u64);
  r[3].u64 = lbz(199);
  m.Store8(sp + 130, static_cast<uint8_t>(r[4].u64));
  m.Store8(sp + 128, static_cast<uint8_t>(r[5].u64));
  m.Store8(sp + 129, static_cast<uint8_t>(r[3].u64));
}
loc_822DD380:
  GuestPack(x, m, formats);  // bl 0x82473fa0
  r[28].u64 = static_cast<uint64_t>(AsS64(r[28]) - 1);
  r[31].u64 = r[31].u64 + 4;
  r[29].u64 = r[29].u64 + 4;
  if (static_cast<int32_t>(r[28].u32()) != 0) goto loc_822DD230;
loc_822DD394:
  r[26].u64 = static_cast<uint64_t>(AsS64(r[26]) - 1);
  if (static_cast<int32_t>(r[26].u32()) != 0) goto loc_822DD220;
}
// ---- end of the recompiled loop ----

// OGRE 1.7's native-endian formats (pixel_util_test's table): index, elem bytes, flags, bits,
// masks, shifts.
struct Row {
  uint32_t index;
  uint8_t elem_bytes;
  uint32_t flags;
  uint8_t bits[4];
  uint32_t masks[4];
  uint8_t shifts[4];
};
constexpr Row kFormats[] = {
    {1, 1, 0x30, {8, 0, 0, 0}, {0xFF, 0, 0, 0}, {0, 0, 0, 0}},                                     // L8
    {6, 2, 0x10, {5, 6, 5, 0}, {0xF800, 0x07E0, 0x001F, 0}, {11, 5, 0, 0}},                        // R5G6B5
    {8, 2, 0x11, {4, 4, 4, 4}, {0x0F00, 0x00F0, 0x000F, 0xF000}, {8, 4, 0, 12}},                   // A4R4G4B4
    {10, 3, 0x10, {8, 8, 8, 0}, {0xFF0000, 0x00FF00, 0x0000FF, 0}, {16, 8, 0, 0}},                 // R8G8B8
    {12, 4, 0x11, {8, 8, 8, 8}, {0xFF0000, 0xFF00, 0xFF, 0xFF000000}, {16, 8, 0, 24}},             // A8R8G8B8
    {13, 4, 0x11, {8, 8, 8, 8}, {0xFF, 0xFF00, 0xFF0000, 0xFF000000}, {0, 8, 16, 24}},             // A8B8G8R8
    {14, 4, 0x11, {8, 8, 8, 8}, {0xFF00, 0xFF0000, 0xFF000000, 0xFF}, {8, 16, 24, 0}},             // B8G8R8A8
    {15, 4, 0x11, {10, 10, 10, 2}, {0x3FF00000, 0x000FFC00, 0x000003FF, 0xC0000000}, {20, 10, 0, 30}},  // A2R10G10B10
    {26, 4, 0x10, {8, 8, 8, 0}, {0xFF0000, 0xFF00, 0xFF, 0xFF000000}, {16, 8, 0, 24}},             // X8R8G8B8
    {28, 4, 0x11, {8, 8, 8, 8}, {0xFF000000, 0xFF0000, 0xFF00, 0xFF}, {24, 16, 8, 0}},             // R8G8B8A8
};

PixelFormatDesc Desc(const Row& row) {
  PixelFormatDesc d;
  d.elem_bytes = row.elem_bytes;
  d.flags = row.flags;
  for (int c = 0; c < 4; ++c) {
    d.bits[c] = row.bits[c];
    d.masks[c] = row.masks[c];
    d.shifts[c] = row.shifts[c];
  }
  return d;
}

double FloatAt(float v) { return static_cast<double>(v); }

constexpr uint32_t kSp = 0x100;        // the frame: sp .. sp + 2064
constexpr uint32_t kItem = 0x1000;

uint64_t checks = 0;

void Case(const Row& item_row, const Row& base_row, uint32_t rows, uint32_t columns, int alpha_mode) {
  PixelFormatDesc formats[32];
  formats[item_row.index] = Desc(item_row);
  formats[base_row.index] = Desc(base_row);
  const uint32_t base_at = kItem + rows * columns * 4 + 64;
  TestMemory start;
  const uint32_t memory_size = base_at + rows * columns * 4 + 64;  // the frame, both images, a margin
  start.bytes.resize(memory_size);
  for (auto& b : start.bytes) b = static_cast<uint8_t>(Random());
  // rows/columns where the build keeps them, big-endian
  for (int i = 0; i < 4; ++i) start.bytes[kSp + 140 + i] = static_cast<uint8_t>(columns >> (24 - 8 * i));
  // Item pixels: the alpha byte (A8R8G8B8 and the like) often 0xFF or 0, as item images are.
  for (uint32_t p = 0; p < rows * columns; ++p) {
    const uint32_t k = Random() % 4;
    if (alpha_mode == 1 || (alpha_mode == 0 && k == 0)) {
      for (int i = 0; i < 4; ++i) start.bytes[kItem + 4 * p + i] = 0xFF;
    } else if (alpha_mode == 0 && k == 1) {
      start.bytes[kItem + 4 * p] = 0;
    }
  }
  const float scale = 1.0f / 255.0f, one = 1.0f;

  TestMemory guest = start;
  Regs x;
  x.r[1].u64 = kSp;
  x.r[17].u64 = 255;
  x.r[19].u64 = base_row.index;
  x.r[26].u64 = rows;
  x.r[27].u64 = item_row.index;
  x.r[29].u64 = kItem;
  x.r[31].u64 = base_at;
  x.f[31].f64 = FloatAt(scale);
  x.f[30].f64 = FloatAt(one);
  GuestLoop(x, guest, formats);

  TestMemory host = start;
  torchlight::hooks::WardrobeBlendLayer layer;
  layer.sp = kSp;
  layer.item = kItem;
  layer.base = base_at;
  layer.rows = rows;
  layer.columns = columns;
  layer.item_format = formats[item_row.index];
  layer.base_format = formats[base_row.index];
  layer.alpha_scale = FloatAt(scale);
  layer.one = FloatAt(one);
  torchlight::hooks::BlendWardrobeLayer(host, layer);

  ++checks;
  const std::string name = "item " + std::to_string(item_row.index) + " base " + std::to_string(base_row.index) +
                           " " + std::to_string(rows) + "x" + std::to_string(columns) + " mode " +
                           std::to_string(alpha_mode);
  if (x.r[26].u64 != 0) Fail(name + ": guest r26 not 0");
  for (uint32_t a = 0; a < memory_size; ++a)
    if (guest.bytes[a] != host.bytes[a]) {
      Fail(name + ": byte " + std::to_string(a) + " " + std::to_string(guest.bytes[a]) + " vs " +
           std::to_string(host.bytes[a]));
      break;
    }
}

}  // namespace

int main() {
  const uint32_t sizes[][2] = {{1, 1}, {1, 7}, {3, 1}, {8, 8}, {17, 33}, {64, 64}};
  for (const Row& item : kFormats)
    for (const Row& base : kFormats)
      for (const auto& size : sizes)
        for (int mode = 0; mode < 3; ++mode) Case(item, base, size[0], size[1], mode);
  // Large random images in the formats the game's textures use here (A8R8G8B8).
  for (int i = 0; i < 20; ++i) Case(kFormats[4], kFormats[4], 256, 256, 2);
  std::printf("wardrobe_blend_test: %llu layers, %d failures\n", static_cast<unsigned long long>(checks), failures);
  return failures ? 1 : 0;
}
