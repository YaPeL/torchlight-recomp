// GpuProgramParameters::_updateAutoParams (guest_abi/auto_params.h) replayed as host code
// (--native_auto_params, native mode only, on by default).
//
// Per pass and renderable the guest walks the program's automatic constants and, for each, calls
// the data source's getter and copies the result into the float constant buffer: a switch of 126
// cases over a 20-byte entry list. The values come from the data source's own code (its getters
// are called here exactly as the guest calls them, by vtable slot, with the same arguments, so
// every matrix and colour is the guest's, computed by the guest), and the copies are word copies
// in the guest's byte order; what this removes is the recompiled dispatch and copy loops. An entry
// of a type the game's programs do not use, anywhere in the list, leaves the whole update to the
// guest's implementation.

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include "guest_abi/auto_params.h"
#include "guest_abi/ogre_layout.h"
#include "guest_abi/xbox_memory.h"
#include "hooks/auto_params.h"
#include "hooks/auto_params_hooks.h"

REXCVAR_DEFINE_BOOL(native_auto_params, true, "Torchlight",
                    "Native mode only: GpuProgramParameters::_updateAutoParams runs as host code "
                    "calling the guest's data source getters (hooks/auto_params_hooks.cpp); false "
                    "runs the recompiled function");

namespace {

namespace abi = torchlight::guest_abi;
namespace ap = torchlight::guest_abi::auto_params;
using Act = ap::Act;
using torchlight::hooks::ColourWriteCount;
using torchlight::hooks::MatrixWriteCount;
using torchlight::hooks::SupportedAutoConstant;
using torchlight::hooks::TransposeWords;

bool g_enabled = false;
struct Stats {
  uint64_t calls = 0, guest = 0, entries = 0, unresolved = 0;
} g_stats;
constexpr uint64_t kReportCalls = 500000;

PPCFunc* Resolve(uint32_t target) {
  static uint32_t targets[64];
  static PPCFunc* functions[64];
  static unsigned count = 0;
  for (unsigned i = 0; i < count; ++i)
    if (targets[i] == target) return functions[i];
  PPCFunc* f = rex::runtime::ResolveIndirectFunction(target);
  if (f && count < 64) {
    targets[count] = target;
    functions[count++] = f;
  }
  return f;
}

// One update's guest calls: the data source's getters by vtable slot, and the matrix-vector
// product, with the guest's calling convention (ints in r3.., `this` in r3, or the result slot
// in r3 and `this` in r4 for a value returned by copy; floats in f1).
struct GuestCalls {
  PPCContext& ctx;
  uint8_t* base;
  uint32_t source;
  uint32_t scratch;  // three 16-byte result slots inside the guest's own frame
  bool ok = true;

  uint32_t Call(uint32_t target, uint32_t r3, uint32_t r4, uint32_t r5) {
    PPCFunc* f = Resolve(target);
    if (!f) {
      ok = false;
      if (++g_stats.unresolved == 1) REXLOG_WARN("auto params: no guest function at 0x{:08X}", target);
      return 0;
    }
    ctx.r3.u64 = r3;
    ctx.r4.u64 = r4;
    ctx.r5.u64 = r5;
    f(ctx, base);
    return ctx.r3.u32;
  }
  uint32_t Virtual(uint32_t object, abi::VtableSlot slot) {
    return abi::ReadU32(base, abi::ReadU32(base, object) + slot.byte_offset());
  }
  // A getter returning a reference: the address of the value.
  uint32_t ByReference(abi::VtableSlot slot, uint32_t argument = 0) {
    return Call(Virtual(source, slot), source, argument, 0);
  }
  // A getter returning 16 bytes by value: the slot's address.
  uint32_t ByValue(abi::VtableSlot slot, uint32_t slot_address, uint32_t argument = 0) {
    Call(Virtual(source, slot), slot_address, source, argument);
    return slot_address;
  }
};

// Guest memory to guest memory: the bytes as they are (same byte order on both sides).
void CopyWords(uint8_t* base, uint32_t dst, uint32_t src, uint32_t words) {
  std::memcpy(abi::xbox_memory::HostAddress(base, dst), abi::xbox_memory::HostAddress(base, src), 4 * words);
}

void WriteMatrix(uint8_t* base, uint32_t params, uint32_t floats, uint32_t index, uint32_t matrix,
                 uint32_t element_count) {
  const uint32_t n = MatrixWriteCount(element_count);
  const uint32_t dst = floats + 4 * index;
  if (abi::ReadU8(base, params + ap::params::kTransposeMatrices.offset)) {
    std::array<uint32_t, 16> m;
    std::memcpy(m.data(), abi::xbox_memory::HostAddress(base, matrix), 64);
    const auto t = TransposeWords(m);
    std::memcpy(abi::xbox_memory::HostAddress(base, dst), t.data(), 4 * n);
  } else {
    CopyWords(base, dst, matrix, n);
  }
}

void WriteColour(uint8_t* base, uint32_t floats, uint32_t index, uint32_t colour, uint32_t element_count) {
  CopyWords(base, floats + 4 * index, colour, ColourWriteCount(element_count));
}

// The update as the guest does it; false when it has to be left to the guest.
bool Replay(PPCContext& ctx, uint8_t* base, uint32_t params, uint32_t source, uint32_t mask) {
  const uint32_t begin = abi::ReadU32(base, params + ap::params::kAutoConstantsBegin.offset);
  const uint32_t end = abi::ReadU32(base, params + ap::params::kAutoConstantsEnd.offset);
  if (begin == end) return true;
  if ((abi::ReadU32(base, params + ap::params::kCombinedVariability.offset) >> 16 & mask) == 0) return true;
  for (uint32_t e = begin; e < end; e += ap::entry::kSize)
    if (!SupportedAutoConstant(abi::ReadU32(base, e + ap::entry::kType.offset))) return false;
  abi::WriteU32(base, params + ap::params::kActivePassIterationIndex.offset, 0xFFFFFFFFu);
  const uint32_t floats = abi::ReadU32(base, params + ap::params::kFloatConstants.offset);
  // The guest's frame is 2240 bytes below the caller's stack pointer; our callees get the stack
  // below that, and the by-value results live inside the frame as the guest's did.
  const uint32_t caller_sp = ctx.r1.u32;
  const uint32_t frame = (caller_sp - ap::kFrameBytes) & ~0xFu;
  GuestCalls g{ctx, base, source, frame + 512};
  ctx.r1.u64 = frame;
  for (uint32_t e = begin; e < end && g.ok; e += ap::entry::kSize) {
    if ((abi::ReadU32(base, e + ap::entry::kVariability.offset) >> 16 & mask) == 0) continue;
    const uint32_t index = abi::ReadU32(base, e + ap::entry::kPhysicalIndex.offset);
    const uint32_t count = abi::ReadU32(base, e + ap::entry::kElementCount.offset);
    const uint32_t data = abi::ReadU32(base, e + ap::entry::kData.offset);
    ++g_stats.entries;
    switch (static_cast<Act>(abi::ReadU32(base, e + ap::entry::kType.offset))) {
      case Act::kWorldMatrix: WriteMatrix(base, params, floats, index, g.ByReference(ap::source::kWorldMatrix), count); break;
      case Act::kInverseWorldMatrix: WriteMatrix(base, params, floats, index, g.ByReference(ap::source::kInverseWorldMatrix), count); break;
      case Act::kInverseTransposeWorldMatrix: WriteMatrix(base, params, floats, index, g.ByReference(ap::source::kInverseTransposeWorldMatrix), count); break;
      case Act::kViewMatrix: WriteMatrix(base, params, floats, index, g.ByReference(ap::source::kViewMatrix), count); break;
      case Act::kViewProjMatrix: WriteMatrix(base, params, floats, index, g.ByReference(ap::source::kViewProjMatrix), count); break;
      case Act::kInverseTransposeWorldViewMatrix: WriteMatrix(base, params, floats, index, g.ByReference(ap::source::kInverseTransposeWorldViewMatrix), count); break;
      case Act::kWorldViewProjMatrix: WriteMatrix(base, params, floats, index, g.ByReference(ap::source::kWorldViewProjMatrix), count); break;
      case Act::kTextureViewProjMatrix: WriteMatrix(base, params, floats, index, g.ByReference(ap::source::kTextureViewProjMatrix, data), count); break;
      case Act::kTextureMatrix: WriteMatrix(base, params, floats, index, g.ByReference(ap::source::kTextureTransformMatrix, data), count); break;
      case Act::kWorldMatrixArray3x4: {
        // The first three rows of each matrix, untransposed, 12 floats each (@0x8220342C).
        const uint32_t matrices = g.ByReference(ap::source::kWorldMatrixArray);
        const uint32_t n = g.ByReference(ap::source::kWorldMatrixCount);
        for (uint32_t i = 0; i < n && g.ok; ++i) CopyWords(base, floats + 4 * index + 48 * i, matrices + 64 * i, 12);
        break;
      }
      case Act::kSurfaceDiffuseColour: WriteColour(base, floats, index, g.ByReference(ap::source::kSurfaceDiffuseColour), count); break;
      case Act::kSurfaceSpecularColour: WriteColour(base, floats, index, g.ByReference(ap::source::kSurfaceSpecularColour), count); break;
      case Act::kSurfaceEmissiveColour: WriteColour(base, floats, index, g.ByReference(ap::source::kSurfaceEmissiveColour), count); break;
      case Act::kLightDiffuseColour: WriteColour(base, floats, index, g.ByReference(ap::source::kLightDiffuseColour, data), count); break;
      case Act::kDerivedAmbientLightColour: WriteColour(base, floats, index, g.ByValue(ap::source::kDerivedAmbientLightColour, g.scratch), count); break;
      case Act::kDerivedSceneColour: WriteColour(base, floats, index, g.ByValue(ap::source::kDerivedSceneColour, g.scratch), count); break;
      case Act::kSurfaceShininess: {
        g.Call(g.Virtual(source, ap::source::kSurfaceShininess), source, 0, 0);
        if (g.ok) abi::WriteU32(base, floats + 4 * index, std::bit_cast<uint32_t>(static_cast<float>(ctx.f1.f64)));
        break;
      }
      case Act::kDerivedLightDiffuseColour: {
        // surface diffuse times the light's diffuse with power, component by component, single
        // precision multiplies (@0x82203F10..), written as a colour.
        const uint32_t surface = g.ByReference(ap::source::kSurfaceDiffuseColour);
        const uint32_t light = g.ByValue(ap::source::kLightDiffuseColourWithPower, g.scratch, data);
        if (!g.ok) break;
        for (uint32_t i = 0; i < 4; ++i) {
          const float p = abi::ReadF32(base, surface + 4 * i) * abi::ReadF32(base, light + 4 * i);
          abi::WriteU32(base, g.scratch + 16 + 4 * i, std::bit_cast<uint32_t>(p));
        }
        WriteColour(base, floats, index, g.scratch + 16, count);
        break;
      }
      case Act::kLightPositionViewSpace: {
        // The light as a 4D vector, by value, times the view matrix (the guest's fused product).
        const uint32_t vector = g.ByValue(ap::source::kLightAs4DVector, g.scratch, data);
        const uint32_t view = g.ByReference(ap::source::kViewMatrix);
        if (!g.ok) break;
        g.Call(ap::kMatrixTimesVector4, g.scratch + 16, view, vector);
        WriteColour(base, floats, index, g.scratch + 16, count);
        break;
      }
    }
  }
  ctx.r1.u64 = caller_sp;
  return g.ok;
}

}  // namespace

namespace torchlight::hooks {

void InstallAutoParams(bool native_only) {
  g_enabled = native_only && REXCVAR_GET(native_auto_params);
  REXLOG_INFO("auto params: {} (--native_auto_params={}, native mode {})",
              g_enabled ? "replayed as host code" : "the recompiled function",
              REXCVAR_GET(native_auto_params), native_only ? "on" : "off");
}

}  // namespace torchlight::hooks

extern "C" {

REX_EXTERN(__imp__sub_82201EB0);
REX_FUNC(sub_82201EB0) {
  static_assert(ap::kUpdateAutoParams == 0x82201EB0u);
  if (!g_enabled) {
    __imp__sub_82201EB0(ctx, base);
    return;
  }
  if (++g_stats.calls % kReportCalls == 0) {
    REXLOG_INFO("auto params: {} updates, {} left to the guest, {} entries written, {} unresolved targets",
                g_stats.calls, g_stats.guest, g_stats.entries, g_stats.unresolved);
  }
  const uint32_t params = ctx.r3.u32, source = ctx.r4.u32, mask = ctx.r5.u32 & 0xFFFF;
  const uint32_t sp = ctx.r1.u32;
  if (!Replay(ctx, base, params, source, mask)) {
    ++g_stats.guest;
    ctx.r1.u64 = sp;
    ctx.r3.u64 = params;
    ctx.r4.u64 = source;
    ctx.r5.u64 = mask;
    __imp__sub_82201EB0(ctx, base);
  }
}

}  // extern "C"
