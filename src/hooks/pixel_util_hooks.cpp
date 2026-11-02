// The guest OGRE's 8-bit colour pack and unpack (guest_abi pixel_util kPackColour8,
// kUnpackColour8) on the host for the formats with integer masks and shifts (hooks/pixel_util.h),
// else the guest's own functions. The equipment's texture build packs and unpacks every pixel of
// an item's texture through them (docs/wardrobe-texture.md), as does every other OGRE pixel
// conversion. --native_pixel_util=false keeps the recompiled guest functions everywhere.
//
// Diagnostics builds (TORCHLIGHT_DIAGNOSTICS) add two checks that compare with the guest's own code:
//  - --diag_pixel_util_check: at the first call, every integer format through both functions
//    against the recompiled originals (all values of each channel alone, then random pixels and colours), and
//    the differences logged;
//  - --diag_wardrobe_composite: the digest of the pixels each wardrobe build packs into the
//    character's texture (hooks/pixel_util_hooks.h LogWardrobeComposite).

#include "hooks/pixel_util_hooks.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <span>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <rex/system/xmemory.h>

#include "guest_abi/ogre_layout.h"
#include "guest_abi/pixel_util.h"
#include "guest_abi/wardrobe.h"
#include "hooks/pixel_util.h"
#include "hooks/wardrobe_blend_hooks.h"

REXCVAR_DEFINE_BOOL(native_pixel_util, true, "Torchlight",
                    "Run the guest OGRE's 8-bit colour pack and unpack on the host for the integer "
                    "formats (false: the recompiled guest functions, as before)");

#if TORCHLIGHT_PIXEL_UTIL_DIAGNOSTICS
REXCVAR_DEFINE_BOOL(diag_pixel_util_check, false, "Torchlight",
                    "Diagnostics: at the first colour pack or unpack, compare the host versions with "
                    "the guest's for every format and log the differences");
REXCVAR_DEFINE_BOOL(diag_wardrobe_composite, false, "Torchlight",
                    "Diagnostics: log a digest of the pixels each wardrobe build packs into the "
                    "character's texture");
#endif

namespace {

namespace abi = torchlight::guest_abi;
namespace pu = torchlight::guest_abi::pixel_util;
using torchlight::hooks::PixelFormatDesc;

static_assert(pu::kUnpackColour8.address == 0x82474560);
static_assert(pu::kPackColour8.address == 0x82473FA0);

uint8_t* Guest(uint8_t* base, uint32_t address) {
  return static_cast<uint8_t*>(rex::memory::GuestPtr(base, address));
}

// The formats' descriptions, parsed once from the guest's table with the 44 bytes they came
// from. Each call still compares its entry's bytes in guest memory with those (one 44-byte compare),
// so a table that changed would be parsed again for that call: the result is always what the
// guest's own code would read.
struct CachedFormat {
  std::array<uint8_t, pu::kFormatSize> raw{};
  PixelFormatDesc desc;
};
std::array<CachedFormat, pu::kFormatCount> g_formats;
std::once_flag g_formats_once;

const uint8_t* Entry(uint8_t* base, uint32_t format) {
  return Guest(base, pu::kFormatTable + pu::kFormatSize * format);
}

// The format's description; false for an index outside the table (the guest would read past it:
// its own code does that).
bool Format(uint8_t* base, uint32_t format, PixelFormatDesc& desc) {
  if (format >= pu::kFormatCount) return false;
  std::call_once(g_formats_once, [base] {
    for (uint32_t f = 0; f < pu::kFormatCount; ++f) {
      std::memcpy(g_formats[f].raw.data(), Entry(base, f), pu::kFormatSize);
      g_formats[f].desc = torchlight::hooks::ParsePixelFormat(g_formats[f].raw);
    }
  });
  const uint8_t* entry = Entry(base, format);
  if (std::memcmp(entry, g_formats[format].raw.data(), pu::kFormatSize) == 0) {
    desc = g_formats[format].desc;
  } else {
    std::array<uint8_t, pu::kFormatSize> raw;
    std::memcpy(raw.data(), entry, pu::kFormatSize);
    desc = torchlight::hooks::ParsePixelFormat(raw);
  }
  return true;
}

// unpackColour(r3 r*, r4 g*, r5 b*, r6 a*, r7 format, r8 src) on the host; false: not handled.
bool HostUnpack(PPCContext& ctx, uint8_t* base) {
  PixelFormatDesc desc;
  if (!Format(base, ctx.r7.u32, desc) || !(desc.flags & pu::kNativeEndian)) return false;
  std::array<uint8_t, 4> pixel{};
  const uint32_t src = ctx.r8.u32;
  for (uint32_t i = 0; i < desc.elem_bytes && i < 4; ++i) pixel[i] = *Guest(base, src + i);
  std::array<uint8_t, 4> rgba;
  if (!torchlight::hooks::UnpackColour8(desc, pixel, rgba)) return false;
  // The guest's store order, for outputs that share an address: b, g, r for luminance
  // (@0x824745D0..@0x824745D8), else r, g, b; then a.
  const uint32_t r = ctx.r3.u32, g = ctx.r4.u32, b = ctx.r5.u32, a = ctx.r6.u32;
  // Through abi::WriteBytes, so the store trace sees them (diag/store_trace.cpp).
  if (desc.flags & pu::kLuminance) {
    abi::WriteBytes(base, b, &rgba[2], 1);
    abi::WriteBytes(base, g, &rgba[1], 1);
    abi::WriteBytes(base, r, &rgba[0], 1);
  } else {
    abi::WriteBytes(base, r, &rgba[0], 1);
    abi::WriteBytes(base, g, &rgba[1], 1);
    abi::WriteBytes(base, b, &rgba[2], 1);
  }
  abi::WriteBytes(base, a, &rgba[3], 1);
  return true;
}

// packColour(r3 r, r4 g, r5 b, r6 a, r7 format, r8 dest) on the host; false: not handled.
bool HostPack(PPCContext& ctx, uint8_t* base) {
  PixelFormatDesc desc;
  if (!Format(base, ctx.r7.u32, desc) || !(desc.flags & pu::kNativeEndian)) return false;
  const std::array<uint8_t, 4> rgba{static_cast<uint8_t>(ctx.r3.u32), static_cast<uint8_t>(ctx.r4.u32),
                                    static_cast<uint8_t>(ctx.r5.u32), static_cast<uint8_t>(ctx.r6.u32)};
  std::array<uint8_t, 4> out;
  const int n = torchlight::hooks::PackColour8(desc, rgba, out);
  if (n < 0) return false;
  const uint32_t dest = ctx.r8.u32;
  abi::WriteBytes(base, dest, out.data(), size_t(n));  // seen by the store trace
  return true;
}

#if TORCHLIGHT_PIXEL_UTIL_DIAGNOSTICS
void RunCheck(PPCContext& ctx, uint8_t* base);
// After a pack: the pixel it wrote, when the wardrobe build called it from its texture loop.
void RecordComposite(uint8_t* base, uint32_t lr, uint32_t format, uint32_t dest);
#endif

}  // namespace

namespace torchlight::hooks {

bool NativePixelUtilEnabled() { return REXCVAR_GET(native_pixel_util); }

bool GuestPixelFormat(uint8_t* base, uint32_t format, PixelFormatDesc& desc) { return Format(base, format, desc); }

void LogPixelUtilMode() {
  REXLOG_INFO("pixel colour pack/unpack: {} (--native_pixel_util={})",
              REXCVAR_GET(native_pixel_util) ? "host for the integer formats" : "the recompiled guest code",
              REXCVAR_GET(native_pixel_util));
}

}  // namespace torchlight::hooks

extern "C" {

REX_EXTERN(__imp__sub_82474560);
REX_FUNC(sub_82474560) {
#if TORCHLIGHT_PIXEL_UTIL_DIAGNOSTICS
  RunCheck(ctx, base);
#endif
  if (!REXCVAR_GET(native_pixel_util) || !HostUnpack(ctx, base)) __imp__sub_82474560(ctx, base);
}

REX_EXTERN(__imp__sub_82473FA0);
REX_FUNC(sub_82473FA0) {
#if TORCHLIGHT_PIXEL_UTIL_DIAGNOSTICS
  RunCheck(ctx, base);
  const uint32_t lr = static_cast<uint32_t>(ctx.lr), format = ctx.r7.u32, dest = ctx.r8.u32;
#endif
  if (!REXCVAR_GET(native_pixel_util) || !HostPack(ctx, base)) __imp__sub_82473FA0(ctx, base);
#if TORCHLIGHT_PIXEL_UTIL_DIAGNOSTICS
  RecordComposite(base, lr, format, dest);
  if (lr == torchlight::guest_abi::wardrobe::blend::kPackReturn) torchlight::hooks::WardrobeBlendCheckPacked(base, dest);
#endif
}

}  // extern "C"

#if TORCHLIGHT_PIXEL_UTIL_DIAGNOSTICS
#include "hooks/pixel_util_check.inl"
#else
void torchlight::hooks::LogWardrobeComposite() {}
void torchlight::hooks::RecordWardrobeCompositePixel(uint8_t*, uint32_t, uint32_t) {}
bool torchlight::hooks::WardrobeCompositeRecording() { return false; }
#endif
