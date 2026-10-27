// The wardrobe build's blend loop on the host: the midasm hook WardrobeBlendLoop, after
// `rotlwi r26,r10,0` @0x822DD21C (guest_abi wardrobe::blend), declared by the generated
// sub_822DC6B0 as `if (WardrobeBlendLoop(ctx.r1, r19, r26, ctx.fpscr)) goto loc_822DD39C;`.
// True: the item image's loop done here (hooks/wardrobe_blend.h) and r26 left 0, as the loop leaves
// it; false: the recompiled loop runs.
//
// What the rest of sub_822DC6B0 reads of the loop's state, and so what this must leave as the guest
// would (checked on every build of the game by tools/wardrobe_blend/check_generated.py):
//  - r26, read on a path to 0x822DDF48 (lwz r3,26516(r26)) the analysis cannot rule out: set to 0;
//  - r28..r31, cr0, cr6, xer: never read before written again; left as they are;
//  - the stack temporaries: written as the last iteration leaves them (BlendWardrobeLayer);
//  - the volatile registers: the loop's last instruction before the exit is a call (packColour),
//    after which the guest's code reads none of them before writing it (the PowerPC ABI, as for
//    every hooked function); left as they are. With no columns this falls back, so that holds.
// The inputs it does not get as arguments come from where the loop's registers came from
// (guest_abi wardrobe::blend: r27 and r29 the item's pixel box at r1+432, r31 the base's through
// r1+164, f30/f31 the constants, r17 = 255).

#include "hooks/wardrobe_blend_hooks.h"

#include <bit>
#include <cstdint>
#include <cstring>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include "guest_abi/pixel_util.h"
#include "guest_abi/wardrobe.h"
#include "hooks/pixel_util_hooks.h"
#include "hooks/wardrobe_blend.h"

REXCVAR_DEFINE_BOOL(native_wardrobe_blend, true, "Torchlight",
                    "Run the equipment texture build's blend loop on the host (false: the recompiled "
                    "loop, as before; also with --native_pixel_util=false)");

#if TORCHLIGHT_PIXEL_UTIL_DIAGNOSTICS
REXCVAR_DEFINE_BOOL(diag_wardrobe_blend_check, false, "Torchlight",
                    "Diagnostics: compute each wardrobe blend on the host, run the recompiled loop and "
                    "compare every pixel and the loop's stack temporaries");
#endif

namespace {

namespace blend = torchlight::guest_abi::wardrobe::blend;
namespace pu = torchlight::guest_abi::pixel_util;
using torchlight::hooks::PixelFormatDesc;
using torchlight::hooks::WardrobeBlendLayer;

uint8_t* Guest(uint8_t* base, uint32_t address) {
  return static_cast<uint8_t*>(rex::memory::GuestPtr(base, address));
}

uint32_t Load32(uint8_t* base, uint32_t address) {
  const uint8_t* p = Guest(base, address);
  return uint32_t{p[0]} << 24 | uint32_t{p[1]} << 16 | uint32_t{p[2]} << 8 | p[3];
}

// lfs: the guest's float, as the double the recompiled code holds it in.
double LoadSingle(uint8_t* base, uint32_t address) {
  return static_cast<double>(std::bit_cast<float>(Load32(base, address)));
}

// The loop's inputs; false (the recompiled loop runs) without columns or for a format the host
// pack/unpack does not handle.
bool ReadLayer(uint8_t* base, uint32_t sp, uint32_t base_format, uint32_t rows, WardrobeBlendLayer& layer) {
  layer.sp = sp;
  layer.rows = rows;
  layer.columns = Load32(base, sp + blend::stack::kColumns);
  if (layer.rows == 0 || layer.columns == 0) return false;
  layer.item = Load32(base, sp + blend::stack::kItemData);
  layer.base = Load32(base, Load32(base, sp + blend::stack::kBasePixelBox) + blend::kPixelBoxData);
  if (!torchlight::hooks::GuestPixelFormat(base, Load32(base, sp + blend::stack::kItemFormat), layer.item_format) ||
      !torchlight::hooks::GuestPixelFormat(base, base_format, layer.base_format) ||
      !(layer.item_format.flags & pu::kNativeEndian) || !(layer.base_format.flags & pu::kNativeEndian))
    return false;
  layer.alpha_scale = LoadSingle(base, blend::kAlphaScale);
  layer.one = LoadSingle(base, blend::kOne);
  return true;
}

// The game's memory, for the host loop.
struct GuestMemory {
  uint8_t* base;
  PPCFPSCRRegister& fpscr;
  uint32_t base_format;
  uint8_t Load8(uint32_t a) const { return *Guest(base, a); }
  void Store8(uint32_t a, uint8_t v) { *Guest(base, a) = v; }
  void BeforeBlend() { fpscr.disableFlushMode(); }
  void Packed(uint32_t dest) { torchlight::hooks::RecordWardrobeCompositePixel(base, base_format, dest); }
};

#if TORCHLIGHT_PIXEL_UTIL_DIAGNOSTICS
bool CheckLayer(uint8_t* base, const WardrobeBlendLayer* layer, PPCFPSCRRegister& fpscr);
#endif

}  // namespace

namespace torchlight::hooks {

bool NativeWardrobeBlendEnabled() { return REXCVAR_GET(native_wardrobe_blend) && NativePixelUtilEnabled(); }

}  // namespace torchlight::hooks

bool WardrobeBlendLoop(PPCRegister& r1, PPCRegister& r19, PPCRegister& r26, PPCFPSCRRegister& fpscr) {
#if TORCHLIGHT_PIXEL_UTIL_DIAGNOSTICS
  const bool check = REXCVAR_GET(diag_wardrobe_blend_check);
#else
  constexpr bool check = false;
#endif
  if (!check && !torchlight::hooks::NativeWardrobeBlendEnabled()) return false;
  uint8_t* base = REX_KERNEL_MEMORY()->virtual_membase();
  WardrobeBlendLayer layer;
  const bool handled = ReadLayer(base, r1.u32, r19.u32, r26.u32, layer);
#if TORCHLIGHT_PIXEL_UTIL_DIAGNOSTICS
  if (check) {
    CheckLayer(base, handled ? &layer : nullptr, fpscr);
    return false;
  }
#endif
  if (!handled) return false;
  GuestMemory memory{base, fpscr, r19.u32};
  torchlight::hooks::BlendWardrobeLayer(memory, layer);
  r26.u64 = 0;
  return true;
}

#if TORCHLIGHT_PIXEL_UTIL_DIAGNOSTICS
#include "hooks/wardrobe_blend_check.inl"
#else
void torchlight::hooks::WardrobeBlendCheckPacked(uint8_t*, uint32_t) {}
void torchlight::hooks::LogWardrobeBlendCheck() {}
#endif
