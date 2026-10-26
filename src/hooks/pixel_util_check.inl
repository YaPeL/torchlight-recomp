// Diagnostics for hooks/pixel_util_hooks.cpp (TORCHLIGHT_DIAGNOSTICS), included at its end: the
// comparison with the recompiled originals and the wardrobe texture digest.

#include <atomic>
#include <mutex>
#include <string>

extern "C" {
REX_EXTERN(sub_821CD7F8);
}

namespace {

// The guest's operator new (r3 = 0, r4 = size; r3 = the block), for the check's scratch memory:
// sub_822DC6B0 allocates its 44-byte image the same way (@0x822DD068). Never freed: one block.

uint64_t g_seed = 0x243F6A8885A308D3ull;
uint32_t Random() {
  g_seed ^= g_seed << 13;
  g_seed ^= g_seed >> 7;
  g_seed ^= g_seed << 17;
  return static_cast<uint32_t>(g_seed);
}

void WriteBig(uint8_t* base, uint32_t at, uint32_t value, uint32_t bytes) {
  for (uint32_t i = 0; i < bytes; ++i) *Guest(base, at + i) = static_cast<uint8_t>(value >> (8 * (bytes - 1 - i)));
}

struct CheckStats {
  uint64_t calls = 0, differences = 0;
};

// One unpack of the pixel at `pixel` through the guest's function and through the hook entry
// (sub_82474560, as callers reach it); the four output bytes compared.
void CompareUnpack(PPCContext& ctx, uint8_t* base, uint32_t scratch, uint32_t format, CheckStats& s,
                   std::string& first) {
  const uint32_t pixel = scratch, guest_out = scratch + 16, host_out = scratch + 32;
  for (uint32_t i = 0; i < 8; ++i) *Guest(base, guest_out + i) = *Guest(base, host_out + i) = 0xA5;
  PPCContext a = ctx;
  a.r3.u64 = guest_out; a.r4.u64 = guest_out + 1; a.r5.u64 = guest_out + 2; a.r6.u64 = guest_out + 3;
  a.r7.u64 = format; a.r8.u64 = pixel;
  __imp__sub_82474560(a, base);
  PPCContext b = ctx;
  b.r3.u64 = host_out; b.r4.u64 = host_out + 1; b.r5.u64 = host_out + 2; b.r6.u64 = host_out + 3;
  b.r7.u64 = format; b.r8.u64 = pixel;
  sub_82474560(b, base);
  ++s.calls;
  for (uint32_t i = 0; i < 4; ++i) {
    if (*Guest(base, guest_out + i) != *Guest(base, host_out + i)) {
      if (!s.differences && first.empty()) {
        first = "unpack format " + std::to_string(format) + " pixel " + std::to_string(*Guest(base, pixel)) + "," +
                std::to_string(*Guest(base, pixel + 1)) + "," + std::to_string(*Guest(base, pixel + 2)) + "," +
                std::to_string(*Guest(base, pixel + 3));
      }
      ++s.differences;
      break;
    }
  }
}

// One pack of r, g, b, a through both, into destinations with the same random bytes; eight bytes
// compared (a pack that writes fewer leaves the rest as it was).
void ComparePack(PPCContext& ctx, uint8_t* base, uint32_t scratch, uint32_t format, std::array<uint8_t, 4> rgba,
                 CheckStats& s, std::string& first) {
  const uint32_t guest_out = scratch + 48, host_out = scratch + 64;
  for (uint32_t i = 0; i < 8; ++i) *Guest(base, guest_out + i) = *Guest(base, host_out + i) = static_cast<uint8_t>(Random());
  PPCContext a = ctx;
  a.r3.u64 = rgba[0]; a.r4.u64 = rgba[1]; a.r5.u64 = rgba[2]; a.r6.u64 = rgba[3];
  a.r7.u64 = format; a.r8.u64 = guest_out;
  __imp__sub_82473FA0(a, base);
  PPCContext b = ctx;
  b.r3.u64 = rgba[0]; b.r4.u64 = rgba[1]; b.r5.u64 = rgba[2]; b.r6.u64 = rgba[3];
  b.r7.u64 = format; b.r8.u64 = host_out;
  sub_82473FA0(b, base);
  ++s.calls;
  for (uint32_t i = 0; i < 8; ++i) {
    if (*Guest(base, guest_out + i) != *Guest(base, host_out + i)) {
      if (!s.differences && first.empty()) {
        first = "pack format " + std::to_string(format) + " rgba " + std::to_string(rgba[0]) + "," +
                std::to_string(rgba[1]) + "," + std::to_string(rgba[2]) + "," + std::to_string(rgba[3]);
      }
      ++s.differences;
      break;
    }
  }
}

std::atomic<bool> g_checking{false};

void RunCheck(PPCContext& ctx, uint8_t* base) {
  static std::once_flag once;
  if (!REXCVAR_GET(diag_pixel_util_check) || g_checking.load(std::memory_order_relaxed)) return;
  std::call_once(once, [&] {
    g_checking = true;
    PPCContext alloc = ctx;
    alloc.r3.u64 = 0;
    alloc.r4.u64 = 128;
    sub_821CD7F8(alloc, base);
    const uint32_t scratch = alloc.r3.u32;
    if (!scratch) {
      REXLOG_ERROR("diag pixel util check: no scratch memory; not run");
      g_checking = false;
      return;
    }
    constexpr uint32_t kRandom = 100000;
    CheckStats total;
    uint32_t checked = 0;
    for (uint32_t format = 0; format < pu::kFormatCount; ++format) {
      PixelFormatDesc desc;
      Format(base, format, desc);
      // Only the formats the host versions take: the others go to the guest's function untouched
      // (HostUnpack / HostPack decline them), and several of those throw in OGRE ("not
      // implemented" for compressed formats: DbgBreakPoint on this build), as on the console.
      if (!(desc.flags & pu::kNativeEndian)) continue;
      CheckStats s;
      std::string first;
      const uint32_t bytes = desc.elem_bytes >= 1 && desc.elem_bytes <= 4 ? desc.elem_bytes : 4;
      // Unpack: every value of each channel alone (the others random), then random pixels.
      for (int c = 0; c < 4; ++c) {
        const uint32_t bits = desc.bits[c] > 16 ? 16 : desc.bits[c];
        if (!bits || !desc.masks[c]) continue;
        for (uint32_t v = 0; v < (1u << bits); ++v) {
          const uint32_t raw = (Random() & ~desc.masks[c]) | ((v << desc.shifts[c]) & desc.masks[c]);
          WriteBig(base, scratch, raw, bytes);
          CompareUnpack(ctx, base, scratch, format, s, first);
        }
      }
      for (uint32_t i = 0; i < kRandom; ++i) {
        WriteBig(base, scratch, Random(), 4);
        CompareUnpack(ctx, base, scratch, format, s, first);
      }
      // Pack: every 8-bit value of each channel alone (the others random), then random colours.
      for (int c = 0; c < 4; ++c) {
        for (uint32_t v = 0; v < 256; ++v) {
          std::array<uint8_t, 4> rgba{static_cast<uint8_t>(Random()), static_cast<uint8_t>(Random()),
                                      static_cast<uint8_t>(Random()), static_cast<uint8_t>(Random())};
          rgba[c] = static_cast<uint8_t>(v);
          ComparePack(ctx, base, scratch, format, rgba, s, first);
        }
      }
      for (uint32_t i = 0; i < kRandom; ++i) {
        const uint32_t r = Random();
        ComparePack(ctx, base, scratch, format,
                    {static_cast<uint8_t>(r), static_cast<uint8_t>(r >> 8), static_cast<uint8_t>(r >> 16),
                     static_cast<uint8_t>(r >> 24)},
                    s, first);
      }
      REXLOG_INFO("diag pixel util check: format {:2} flags 0x{:02X} {} bytes: {} calls, {} differences{}{}", format,
                  desc.flags, desc.elem_bytes, s.calls, s.differences, first.empty() ? "" : "; first: ", first);
      ++checked;
      total.calls += s.calls;
      total.differences += s.differences;
    }
    REXLOG_INFO("diag pixel util check: the {} integer formats, {} calls (native_pixel_util={}), {} differences",
                checked, total.calls, REXCVAR_GET(native_pixel_util), total.differences);
    g_checking = false;
  });
}

// The pixels the wardrobe build's texture loop packed (its packColour call returns to
// 0x822DD384), digested in order with their destinations, per build.
constexpr uint32_t kWardrobeCompositePackReturn = 0x822DD384;
std::mutex g_composite_mutex;
uint64_t g_composite_digest = 0xCBF29CE484222325ull;
uint64_t g_composite_pixels = 0;
uint32_t g_composite_first = 0;  // the build's first destination: the digest takes offsets from it

void Mix(uint64_t value) {
  g_composite_digest ^= value;
  g_composite_digest *= 0x100000001B3ull;
}

void RecordComposite(uint8_t* base, uint32_t lr, uint32_t format, uint32_t dest) {
  if (!REXCVAR_GET(diag_wardrobe_composite) || lr != kWardrobeCompositePackReturn ||
      g_checking.load(std::memory_order_relaxed))
    return;
  PixelFormatDesc desc;
  const uint32_t bytes = Format(base, format, desc) && desc.elem_bytes <= 4 ? desc.elem_bytes : 4;
  uint32_t value = 0;
  for (uint32_t i = 0; i < bytes; ++i) value = value << 8 | *Guest(base, dest + i);
  std::lock_guard lock(g_composite_mutex);
  if (!g_composite_pixels) g_composite_first = dest;
  Mix(dest - g_composite_first);
  Mix(value);
  ++g_composite_pixels;
}

}  // namespace

void torchlight::hooks::LogWardrobeComposite() {
  if (!REXCVAR_GET(diag_wardrobe_composite)) return;
  std::lock_guard lock(g_composite_mutex);
  if (!g_composite_pixels) return;
  REXLOG_INFO("diag wardrobe composite: {} pixels, digest {:016X} (native_pixel_util={})", g_composite_pixels,
              g_composite_digest, REXCVAR_GET(native_pixel_util));
  g_composite_digest = 0xCBF29CE484222325ull;
  g_composite_pixels = 0;
}
