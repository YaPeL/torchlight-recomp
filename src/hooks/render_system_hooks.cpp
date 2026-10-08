#include "hooks/hooks.h"

#include <algorithm>
#include <array>
#include <bit>
#include <initializer_list>
#include <span>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include "capture/guest_readers.h"
#include "capture/session.h"
#include "capture/translate.h"
#include "guest_abi/ogre_enums.h"
#include "guest_abi/guest_functions.h"
#include "guest_abi/ogre_layout.h"
#include "hooks/guest_d3d_skip.h"

REXCVAR_DEFINE_BOOL(native_skip_guest_d3d, false, "Torchlight",
                    "Native mode only: skip the guest's D3D work for RenderSystem calls whose only "
                    "effect is the Xenos device (docs/native-skip-guest-d3d.md)");

namespace {

using torchlight::capture::Session;
namespace cmd = torchlight::commands;
namespace cap = torchlight::capture;
namespace abi = torchlight::guest_abi;
namespace ogre = torchlight::guest_abi::ogre;

// InstallGuestD3DSkip's decision for the session (guest_d3d_skip.h).
bool g_skip_guest_d3d = false;

uint32_t R(const PPCRegister& r) { return r.u32; }
float F(const PPCRegister& r) { return static_cast<float>(r.f64); }

// Ties every override to the guest_abi slot table: the symbol address must be the slot's
// implementation in the guest D3D9RenderSystem vtable.
#define SLOT_ADDRESS_CHECK(slot, addr) \
  static_assert(ogre::kRenderSystemSlots[slot].guest_impl == 0x##addr##u, "slot table mismatch")

// Count-only override.
#define COUNT_HOOK(slot, addr)                       \
  SLOT_ADDRESS_CHECK(slot, addr);                    \
  REX_EXTERN(__imp__sub_##addr);                     \
  extern "C" REX_FUNC(sub_##addr) {                  \
    Session::Get().CountSlot(slot);                  \
    __imp__sub_##addr(ctx, base);                    \
  }

// Recording override: `body` runs before the original and may use ctx/base/s. The original is
// skipped for the slots of guest_d3d_skip.h when the session decided so.
#define RECORD_HOOK(slot, addr, body)                \
  SLOT_ADDRESS_CHECK(slot, addr);                    \
  REX_EXTERN(__imp__sub_##addr);                     \
  extern "C" REX_FUNC(sub_##addr) {                  \
    Session& s = Session::Get();                     \
    s.CountSlot(slot);                               \
    s.set_membase(base);                             \
    {                                                \
      cap::ProducerTimer timer(s.producer_times(),   \
                               cap::ProducerSection::kCommands, s.measuring()); \
      cap::HookTimer hook_timer(s.hook_costs(), slot, s.measuring()); \
      body;                                          \
    }                                                \
    if constexpr (torchlight::hooks::SkippableSlot(slot)) { \
      if (g_skip_guest_d3d) return;                  \
    }                                                \
    __imp__sub_##addr(ctx, base);                    \
  }

template <typename T>
uint32_t Key(uint32_t sub = 0) {
  return cap::ShadowKey(cmd::CommandPayload(std::in_place_type<T>), sub);
}

uint32_t Bits(float f) { return std::bit_cast<uint32_t>(f); }

// State whose command depends only on `raw` (Session::Unchanged): built and recorded only when
// the raw words changed since the last call for (slot, sub) or the shadow entry was rewritten.
template <typename Build>
void MemoState(Session& s, uint32_t slot, uint32_t sub, uint32_t key,
               std::initializer_list<uint32_t> raw, Build build) {
  std::span<const uint32_t> words(raw.begin(), raw.size());
  if (s.Unchanged(slot, sub, words)) return;
  s.State(key, build());
  s.Remember(slot, sub, key, words);
}

// The 16 words of a guest Matrix4, raw.
std::array<uint32_t, 16> MatrixWords(const uint8_t* base, uint32_t address) {
  std::array<uint32_t, 16> w;
  for (uint32_t i = 0; i < 16; ++i) w[i] = abi::ReadU32(base, address + 4 * i);
  return w;
}

// State built from a guest Matrix4: the early out compares the matrix's words.
template <typename Build>
void MemoMatrixState(Session& s, uint32_t slot, uint32_t sub, uint32_t key, uint32_t extra,
                     const std::array<uint32_t, 16>& m, Build build) {
  std::array<uint32_t, 17> raw;
  std::copy(m.begin(), m.end(), raw.begin());
  raw[16] = extra;
  if (s.Unchanged(slot, sub, raw)) return;
  s.State(key, build());
  s.Remember(slot, sub, key, raw);
}

}  // namespace

extern "C" {

// ---- frame -----------------------------------------------------------------------------------
RECORD_HOOK(55, 821AE5A8, s.Event(cmd::BeginFrame{}))
RECORD_HOOK(58, 821B1000, s.Event(cmd::EndFrame{}))

// ---- matrices --------------------------------------------------------------------------------
RECORD_HOOK(28, 821DD450, MemoMatrixState(s, 28, 0, Key<cmd::SetMatrix>(0), 0,
                                           MatrixWords(base, R(ctx.r4)), [&] {
  cmd::SetMatrix c;
  c.kind = {uint8_t(cmd::MatrixKind::kWorld), 0};
  c.m = cap::ReadMatrix(base, R(ctx.r4));
  return c;
}))
RECORD_HOOK(30, 821BAC40, MemoMatrixState(s, 30, 0, Key<cmd::SetMatrix>(1), 0,
                                           MatrixWords(base, R(ctx.r4)), [&] {
  cmd::SetMatrix c;
  c.kind = {uint8_t(cmd::MatrixKind::kView), 1};
  c.m = cap::ReadMatrix(base, R(ctx.r4));
  return c;
}))
RECORD_HOOK(31, 821BDC50, MemoMatrixState(s, 31, 0, Key<cmd::SetMatrix>(2), 0,
                                           MatrixWords(base, R(ctx.r4)), [&] {
  cmd::SetMatrix c;
  c.kind = {uint8_t(cmd::MatrixKind::kProjection), 2};
  c.m = cap::ReadMatrix(base, R(ctx.r4));
  return c;
}))
RECORD_HOOK(29, 824C3CB8, ({
  cmd::SetWorldMatrices c;
  uint32_t count = R(ctx.r5) & 0xFFFF;
  for (uint32_t i = 0; i < count && i < 256; ++i) {
    c.matrices.push_back(cap::ReadMatrix(base, R(ctx.r4) + i * ogre::matrix4::kSize.bytes));
  }
  s.State(Key<cmd::SetWorldMatrices>(), c);
}))
RECORD_HOOK(50, 821CA930, MemoMatrixState(s, 50, R(ctx.r4), Key<cmd::SetMatrix>(0x10 + R(ctx.r4)),
                                           R(ctx.r4), MatrixWords(base, R(ctx.r5)), [&] {
  cmd::SetMatrix c;
  c.kind = {uint8_t(cmd::MatrixKind::kTexture), 3};
  c.texture_unit = R(ctx.r4);
  c.m = cap::ReadMatrix(base, R(ctx.r5));
  return c;
}))

// ---- targets, viewport, clear -----------------------------------------------------------------
RECORD_HOOK(114, 821A7620, s.State(Key<cmd::SetRenderTarget>(),
                                   cap::ReadRenderTarget(base, R(ctx.r4))))
RECORD_HOOK(59, 821A4368, s.State(Key<cmd::SetViewport>(), cap::ReadViewport(base, R(ctx.r4))))
RECORD_HOOK(107, 8219CAF8, ({
  if (s.recording()) {
    cmd::Clear c;
    c.buffers = R(ctx.r4);
    c.colour = cap::ReadColour(base, R(ctx.r5));
    c.depth = F(ctx.f1);
    c.stencil = R(ctx.r6);
    s.Event(c);
  }
}))

// ---- textures and samplers -------------------------------------------------------------------
RECORD_HOOK(39, 821C8E40, ({
  cmd::SetTexture c;
  c.unit = R(ctx.r4);
  c.enabled = (R(ctx.r5) & 0xFF) != 0;
  uint32_t texture = abi::ReadU32(base, R(ctx.r6) + ogre::shared_ptr::kPRep.offset);
  if (c.enabled) c.texture = cap::CaptureTexture(base, texture);
  s.State(Key<cmd::SetTexture>(c.unit), c, c.enabled ? texture : 0);
}))
RECORD_HOOK(40, 821CA710, ({
  cmd::SetTexture c;
  c.unit = R(ctx.r4);
  c.vertex_texture = true;
  uint32_t texture = abi::ReadU32(base, R(ctx.r5) + ogre::shared_ptr::kPRep.offset);
  c.enabled = texture != 0;
  if (c.enabled) c.texture = cap::CaptureTexture(base, texture);
  s.State(Key<cmd::SetTexture>(0x100 + c.unit), c, texture);
}))
RECORD_HOOK(33, 821DC8D8, ({
  cmd::DisableTextureUnit c{R(ctx.r4)};
  s.State(Key<cmd::SetTexture>(c.unit), cmd::SetTexture{c.unit, false, false, std::nullopt});
  s.Event(c);
}))
RECORD_HOOK(44, 821CA470, ({
  cmd::SetSamplerFilter c;
  c.unit = R(ctx.r4);
  c.stage = cap::ToFilterStage(R(ctx.r5));
  c.filter = cap::ToFilter(R(ctx.r6));
  uint32_t sub = c.unit * 4 + (c.stage.raw & 3);
  MemoState(s, 44, sub, Key<cmd::SetSamplerFilter>(sub), {R(ctx.r4), R(ctx.r5), R(ctx.r6)},
            [&] { return c; });
}))
RECORD_HOOK(46, 821CAD08,
            MemoState(s, 46, R(ctx.r4), Key<cmd::SetSamplerAnisotropy>(R(ctx.r4)),
                      {R(ctx.r4), R(ctx.r5)},
                      [&] { return cmd::SetSamplerAnisotropy{R(ctx.r4), R(ctx.r5)}; }))
RECORD_HOOK(47, 821CA1A8, ({
  uint32_t uvw = R(ctx.r5);
  uint32_t u = abi::ReadU32(base, uvw, ogre::uvw_addressing_mode::kU);
  uint32_t v = abi::ReadU32(base, uvw, ogre::uvw_addressing_mode::kV);
  uint32_t w = abi::ReadU32(base, uvw, ogre::uvw_addressing_mode::kW);
  MemoState(s, 47, R(ctx.r4), Key<cmd::SetSamplerAddress>(R(ctx.r4)), {R(ctx.r4), u, v, w}, [&] {
    cmd::SetSamplerAddress c;
    c.unit = R(ctx.r4);
    c.uvw[0] = cap::ToAddress(u);
    c.uvw[1] = cap::ToAddress(v);
    c.uvw[2] = cap::ToAddress(w);
    return c;
  });
}))
RECORD_HOOK(48, 82572D58, ({
  uint32_t colour = R(ctx.r5);
  MemoState(s, 48, R(ctx.r4), Key<cmd::SetSamplerBorder>(R(ctx.r4)),
            {R(ctx.r4), abi::ReadU32(base, colour), abi::ReadU32(base, colour + 4),
             abi::ReadU32(base, colour + 8), abi::ReadU32(base, colour + 12)},
            [&] { return cmd::SetSamplerBorder{R(ctx.r4), cap::ReadColour(base, colour)}; });
}))
RECORD_HOOK(49, 821CA880,
            MemoState(s, 49, R(ctx.r4), Key<cmd::SetSamplerMipBias>(R(ctx.r4)),
                      {R(ctx.r4), Bits(F(ctx.f1))},
                      [&] { return cmd::SetSamplerMipBias{R(ctx.r4), F(ctx.f1)}; }))
RECORD_HOOK(41, 821CAD88,
            MemoState(s, 41, R(ctx.r4), Key<cmd::SetTexCoordSet>(R(ctx.r4)), {R(ctx.r4), R(ctx.r5)},
                      [&] { return cmd::SetTexCoordSet{R(ctx.r4), R(ctx.r5)}; }))
RECORD_HOOK(42, 821CB0F0,
            MemoState(s, 42, R(ctx.r4), Key<cmd::SetTexCoordCalc>(R(ctx.r4)), {R(ctx.r4), R(ctx.r5)},
                      [&] { return cmd::SetTexCoordCalc{R(ctx.r4), R(ctx.r5)}; }))

RECORD_HOOK(43, 821C9100, ({
  namespace lb = ogre::layer_blend_mode_ex;
  uint32_t unit = R(ctx.r4);
  uint32_t mode = R(ctx.r5);
  uint32_t blend_type = abi::ReadU32(base, mode, lb::kBlendType);
  uint32_t c1 = mode + lb::kColourArg1.offset, c2 = mode + lb::kColourArg2.offset;
  uint32_t sub = unit * 2 + (blend_type & 1);
  MemoState(s, 43, sub, Key<cmd::SetTextureBlend>(sub),
            {unit, blend_type, abi::ReadU32(base, mode, lb::kOperation),
             abi::ReadU32(base, mode, lb::kSource1), abi::ReadU32(base, mode, lb::kSource2),
             abi::ReadU32(base, c1), abi::ReadU32(base, c1 + 4), abi::ReadU32(base, c1 + 8),
             abi::ReadU32(base, c1 + 12), abi::ReadU32(base, c2), abi::ReadU32(base, c2 + 4),
             abi::ReadU32(base, c2 + 8), abi::ReadU32(base, c2 + 12),
             abi::ReadU32(base, mode, lb::kAlphaArg1), abi::ReadU32(base, mode, lb::kAlphaArg2),
             abi::ReadU32(base, mode, lb::kFactor)},
            [&] {
              cmd::SetTextureBlend c;
              c.unit = unit;
              c.raw_blend_type = blend_type;
              c.raw_operation = abi::ReadU32(base, mode, lb::kOperation);
              c.raw_source1 = abi::ReadU32(base, mode, lb::kSource1);
              c.raw_source2 = abi::ReadU32(base, mode, lb::kSource2);
              c.is_modulate = c.raw_operation == ogre::layer_blend_operation_ex::kModulate.value;
              c.colour_arg1 = cap::ReadColour(base, c1);
              c.colour_arg2 = cap::ReadColour(base, c2);
              c.alpha_arg1 = abi::ReadF32(base, mode, lb::kAlphaArg1);
              c.alpha_arg2 = abi::ReadF32(base, mode, lb::kAlphaArg2);
              c.factor = abi::ReadF32(base, mode, lb::kFactor);
              return c;
            });
}))

// ---- blend, depth, raster --------------------------------------------------------------------
RECORD_HOOK(51, 821C52B0,
            MemoState(s, 51, 0, Key<cmd::SetBlend>(), {R(ctx.r4), R(ctx.r5), R(ctx.r6)}, [&] {
  cmd::SetBlend c;
  c.src = cap::ToBlendFactor(R(ctx.r4));
  c.dst = cap::ToBlendFactor(R(ctx.r5));
  c.op = cap::ToBlendOp(R(ctx.r6));
  c.src_alpha = c.src;
  c.dst_alpha = c.dst;
  c.op_alpha = c.op;
  return c;
}))
RECORD_HOOK(52, 82572E30,
            MemoState(s, 52, 0, Key<cmd::SetBlend>(),
                      {R(ctx.r4), R(ctx.r5), R(ctx.r6), R(ctx.r7), R(ctx.r8), R(ctx.r9)}, [&] {
  cmd::SetBlend c;
  c.separate = true;
  c.src = cap::ToBlendFactor(R(ctx.r4));
  c.dst = cap::ToBlendFactor(R(ctx.r5));
  c.src_alpha = cap::ToBlendFactor(R(ctx.r6));
  c.dst_alpha = cap::ToBlendFactor(R(ctx.r7));
  c.op = cap::ToBlendOp(R(ctx.r8));
  c.op_alpha = cap::ToBlendOp(R(ctx.r9));
  return c;
}))
RECORD_HOOK(53, 821C54D8,
            MemoState(s, 53, 0, Key<cmd::SetAlphaReject>(), {R(ctx.r4), R(ctx.r5), R(ctx.r6)}, [&] {
  cmd::SetAlphaReject c;
  c.func = cap::ToCompare(R(ctx.r4));
  c.reference = R(ctx.r5) & 0xFF;
  c.alpha_to_coverage = (R(ctx.r6) & 0xFF) != 0;
  return c;
}))
RECORD_HOOK(64, 821C2F38,
            MemoState(s, 64, 0, Key<cmd::SetDepthCheck>(), {R(ctx.r4) & 0xFF},
                      [&] { return cmd::SetDepthCheck{(R(ctx.r4) & 0xFF) != 0}; }))
RECORD_HOOK(65, 821C4E88,
            MemoState(s, 65, 0, Key<cmd::SetDepthWrite>(), {R(ctx.r4) & 0xFF},
                      [&] { return cmd::SetDepthWrite{(R(ctx.r4) & 0xFF) != 0}; }))
RECORD_HOOK(66, 821C3010,
            MemoState(s, 66, 0, Key<cmd::SetDepthFunc>(), {R(ctx.r4)}, [&] {
  cmd::SetDepthFunc c;
  c.requested = cap::ToCompare(R(ctx.r4));
  c.effective = cap::ToCompare(ogre::RunicEffectiveDepthFunction(R(ctx.r4)));
  return c;
}))
RECORD_HOOK(68, 821D14B8,
            MemoState(s, 68, 0, Key<cmd::SetDepthBias>(), {Bits(F(ctx.f1)), Bits(F(ctx.f2))},
                      [&] { return cmd::SetDepthBias{F(ctx.f1), F(ctx.f2)}; }))
RECORD_HOOK(61, 821C6CF8,
            MemoState(s, 61, 0, Key<cmd::SetCull>(), {R(ctx.r4)},
                      [&] { return cmd::SetCull{cap::ToCull(R(ctx.r4))}; }))
RECORD_HOOK(67, 821C3520,
            MemoState(s, 67, 0, Key<cmd::SetColourWrite>(),
                      {R(ctx.r4) & 0xFF, R(ctx.r5) & 0xFF, R(ctx.r6) & 0xFF, R(ctx.r7) & 0xFF},
                      [&] {
                        return cmd::SetColourWrite{(R(ctx.r4) & 0xFF) != 0, (R(ctx.r5) & 0xFF) != 0,
                                                   (R(ctx.r6) & 0xFF) != 0, (R(ctx.r7) & 0xFF) != 0};
                      }))
RECORD_HOOK(81, 821C5228,
            MemoState(s, 81, 0, Key<cmd::SetPolygonMode>(), {R(ctx.r4)},
                      [&] { return cmd::SetPolygonMode{cap::ToPolygonMode(R(ctx.r4))}; }))
RECORD_HOOK(82, 825730A0,
            MemoState(s, 82, 0, Key<cmd::SetStencilCheck>(), {R(ctx.r4) & 0xFF},
                      [&] { return cmd::SetStencilCheck{(R(ctx.r4) & 0xFF) != 0}; }))
RECORD_HOOK(83, 825730F8,
            MemoState(s, 83, 0, Key<cmd::SetStencil>(),
                      {R(ctx.r4), R(ctx.r5), R(ctx.r6), R(ctx.r7), R(ctx.r8), R(ctx.r9),
                       R(ctx.r10) & 0xFF},
                      [&] {
  cmd::SetStencil c;
  c.func = cap::ToCompare(R(ctx.r4));
  c.reference = R(ctx.r5);
  c.mask = R(ctx.r6);
  c.fail = cap::ToStencilOp(R(ctx.r7));
  c.depth_fail = cap::ToStencilOp(R(ctx.r8));
  c.pass = cap::ToStencilOp(R(ctx.r9));
  c.two_sided = (R(ctx.r10) & 0xFF) != 0;
  return c;
}))
RECORD_HOOK(106, 825739A8,
            MemoState(s, 106, 0, Key<cmd::SetScissor>(),
                      {R(ctx.r4) & 0xFF, R(ctx.r5), R(ctx.r6), R(ctx.r7), R(ctx.r8)}, [&] {
  return cmd::SetScissor{(R(ctx.r4) & 0xFF) != 0, R(ctx.r5), R(ctx.r6), R(ctx.r7), R(ctx.r8)};
}))
RECORD_HOOK(124, 82573870, ({
  cmd::SetClipPlanes c;
  uint32_t list = R(ctx.r4);
  uint32_t first = abi::ReadU32(base, list + ogre::stl_vector::kFirst.offset);
  uint32_t last = abi::ReadU32(base, list + ogre::stl_vector::kLast.offset);
  for (uint32_t p = first; p != 0 && p < last && c.planes.size() < 32;
       p += ogre::plane::kSize.bytes) {
    c.planes.push_back({abi::ReadF32(base, p), abi::ReadF32(base, p + 4),
                        abi::ReadF32(base, p + 8), abi::ReadF32(base, p + ogre::plane::kD.offset)});
  }
  s.State(Key<cmd::SetClipPlanes>(), c);
}))
RECORD_HOOK(36, 821D2530,
            MemoState(s, 36, 0, Key<cmd::SetPointSprites>(), {R(ctx.r4) & 0xFF},
                      [&] { return cmd::SetPointSprites{(R(ctx.r4) & 0xFF) != 0}; }))
RECORD_HOOK(104, 8219BEB0,
            MemoState(s, 104, 0, Key<cmd::SetInvertWinding>(), {R(ctx.r4) & 0xFF},
                      [&] { return cmd::SetInvertWinding{(R(ctx.r4) & 0xFF) != 0}; }))
RECORD_HOOK(113, 821CEF20,
            MemoState(s, 113, 0, Key<cmd::SetDeriveDepthBias>(),
                      {R(ctx.r4) & 0xFF, Bits(F(ctx.f1)), Bits(F(ctx.f2)), Bits(F(ctx.f3))}, [&] {
  return cmd::SetDeriveDepthBias{(R(ctx.r4) & 0xFF) != 0, F(ctx.f1), F(ctx.f2), F(ctx.f3)};
}))
RECORD_HOOK(112, 821CEF38,
            MemoState(s, 112, 0, Key<cmd::SetPassIterationCount>(), {R(ctx.r4)},
                      [&] { return cmd::SetPassIterationCount{R(ctx.r4)}; }))

// ---- geometry and programs -------------------------------------------------------------------
RECORD_HOOK(84, 821CE5A0, ({
  uint32_t declaration = R(ctx.r4);
  s.State(Key<cmd::SetVertexDeclaration>(), cap::CaptureVertexDeclaration(base, declaration),
          declaration);
}))
RECORD_HOOK(85, 821C3E78, s.State(Key<cmd::SetVertexBuffers>(),
                                  cap::ReadVertexBufferBinding(base, R(ctx.r4))))
RECORD_HOOK(90, 821C1398, ({
  uint32_t program = R(ctx.r4);
  cmd::BindProgram c = cap::CaptureProgram(base, program);
  s.State(Key<cmd::BindProgram>(c.stage.raw), c, program);
}))
RECORD_HOOK(93, 821BE470, ({
  cmd::UnbindProgram c{cap::ToStage(R(ctx.r4))};
  s.EraseState(Key<cmd::BindProgram>(c.stage.raw));
  s.Event(c);
}))
RECORD_HOOK(91, 821C2118, ({
  if (s.recording()) {
    cap::ProducerTimer constants(s.producer_times(), cap::ProducerSection::kConstants,
                                 s.measuring());
    uint32_t parameters = abi::ReadU32(base, R(ctx.r5) + ogre::shared_ptr::kPRep.offset);
    // Live commands send only the constants the backend does not hold yet, and no command when
    // nothing changes; a capture being recorded gets every range (capture/constant_mirror.h).
    cap::ConstantMirror* mirror = s.live() ? &s.constant_mirror() : nullptr;
    const bool filter = mirror && !s.armed();
    bool unchanged = false;
    cmd::SetConstants c = cap::ReadConstants(base, parameters, R(ctx.r4), R(ctx.r6) & 0xFFFF,
                                             mirror, filter, &unchanged);
    // (No return here: the guest's own bindGpuProgramParameters runs after this body.)
    if (!unchanged) s.Event(std::move(c));
  }
}))

}  // extern "C"

// ---- draw ------------------------------------------------------------------------------------
SLOT_ADDRESS_CHECK(87, 821C4058);
REX_EXTERN(__imp__sub_821C4058);
extern "C" REX_FUNC(sub_821C4058) {
  Session& s = Session::Get();
  s.CountSlot(87);
  uint32_t render_system = R(ctx.r3);
  uint32_t operation = R(ctx.r4);
  __imp__sub_821C4058(ctx, base);  // binds declaration/buffers and uploads them first
  if (s.recording()) {
    cap::ProducerTimer timer(s.producer_times(), cap::ProducerSection::kCommands, s.measuring());
    cap::HookTimer hook_timer(s.hook_costs(), 87, s.measuring());
    // Scratch: DrawEvent reads the keys, it does not keep them.
    thread_local std::vector<cmd::Hash> live_keys;
    live_keys.clear();
    cmd::Draw d = cap::CaptureDraw(base, render_system, operation, live_keys);
    s.DrawEvent(std::move(d), live_keys);
  }
}

// ---- device calls skipped as a whole (guest_d3d_skip.h) ------------------------------------
// The draw and the index and stream bindings of the Xbox D3D device, whatever calls them. Callers
// overwrite r3 after each (they return nothing used), so a skipped call leaves the context as is.
#define DEVICE_SKIP_HOOK(entry, addr)                                                   \
  static_assert(torchlight::guest_abi::functions::entry.address == 0x##addr##u);         \
  static_assert(torchlight::hooks::SkippableDeviceCall(0x##addr##u));                    \
  REX_EXTERN(__imp__sub_##addr);                                                        \
  extern "C" REX_FUNC(sub_##addr) {                                                     \
    if (g_skip_guest_d3d) return;                                                       \
    __imp__sub_##addr(ctx, base);                                                       \
  }
DEVICE_SKIP_HOOK(kD3DDrawIndexed, 821CF830)
DEVICE_SKIP_HOOK(kD3DDraw, 821D0A10)
DEVICE_SKIP_HOOK(kD3DSetIndices, 821C39F8)
DEVICE_SKIP_HOOK(kD3DSetStreamSource, 821C3D58)

// ---- count-only slots ------------------------------------------------------------------------
extern "C" {
COUNT_HOOK(0, 8256E4E8)  // ~dtor
COUNT_HOOK(1, 8256E738)  // getName
COUNT_HOOK(2, 8256FE98)  // getConfigOptions
COUNT_HOOK(3, 8256F510)  // setConfigOption
COUNT_HOOK(4, 82573BD0)  // createHardwareOcclusionQuery
COUNT_HOOK(5, 824C3FA8)  // destroyHardwareOcclusionQuery
COUNT_HOOK(6, 8256FB90)  // validateConfigOptions
COUNT_HOOK(7, 8256FEA0)  // _initialise
COUNT_HOOK(8, 82574168)  // createRenderSystemCapabilities
COUNT_HOOK(9, 824C34D0)  // useCustomRenderSystemCapabilities
COUNT_HOOK(10, 82570BC0)  // reinitialise
COUNT_HOOK(11, 82570C70)  // shutdown
COUNT_HOOK(15, 82570E40)  // _createRenderWindow
COUNT_HOOK(16, 825711E0)  // _createRenderWindows
COUNT_HOOK(17, 82572788)  // createMultiRenderTarget
COUNT_HOOK(20, 82572858)  // destroyRenderTarget
COUNT_HOOK(21, 824C37D0)  // attachRenderTarget
COUNT_HOOK(22, 824C38D8)  // getRenderTarget
COUNT_HOOK(23, 824C3928)  // detachRenderTarget
COUNT_HOOK(24, 824C2F80)  // getRenderTargetIterator
COUNT_HOOK(25, 82572938)  // getErrorDescription
COUNT_HOOK(26, 82572A50)  // _useLights
COUNT_HOOK(32, 821C93A0)  // _setTextureUnitSettings
COUNT_HOOK(34, 821BF850)  // _disableTextureUnitsFrom
COUNT_HOOK(37, 821C56A0)  // _setPointParameters
COUNT_HOOK(38, 824C39D8)  // _setTexture
COUNT_HOOK(45, 821CA3F0)  // _setTextureUnitFiltering
COUNT_HOOK(54, 8219B038)  // _setTextureProjectionRelativeTo
COUNT_HOOK(56, 82573538)  // _pauseFrame
COUNT_HOOK(57, 82573688)  // _resumeFrame
COUNT_HOOK(62, 82204268)  // _getCullingMode
COUNT_HOOK(63, 821BE2F8)  // _setDepthBufferParams
COUNT_HOOK(70, 821F28F8)  // _beginGeometryCount
COUNT_HOOK(72, 8219AE58)  // _getBatchCount
COUNT_HOOK(74, 821DB560)  // convertColourValue
COUNT_HOOK(75, 8257F850)  // getColourVertexElementType
COUNT_HOOK(76, 8219AD88)  // _convertProjectionMatrix
COUNT_HOOK(77, 82573AB0)  // _makeProjectionMatrix
COUNT_HOOK(78, 82572970)  // _makeProjectionMatrix
COUNT_HOOK(79, 821ABAE8)  // _makeOrthoMatrix
COUNT_HOOK(80, 82573C80)  // _applyObliqueDepthProjection
COUNT_HOOK(88, 824C2F98)  // getDriverVersion
COUNT_HOOK(89, 824C40E8)  // kRunicSlot89_UnknownStringGetter
COUNT_HOOK(92, 82573730)  // bindGpuProgramPassIterationParameters
COUNT_HOOK(94, 821BF120)  // isGpuProgramBound
COUNT_HOOK(95, 824C3D50)  // setClipPlanes
COUNT_HOOK(96, 824C3D10)  // addClipPlane
COUNT_HOOK(97, 824C3CD8)  // addClipPlane
COUNT_HOOK(98, 824C3DA0)  // resetClipPlanes
COUNT_HOOK(99, 824C3448)  // _initRenderTargets
COUNT_HOOK(100, 824C3DE8)  // _notifyCameraRemoved
COUNT_HOOK(101, 821EE1C0)  // _updateAllRenderTargets
COUNT_HOOK(102, 821EE2F8)  // _swapAllRenderTargetBuffers
COUNT_HOOK(111, 82573D48)  // getMaximumDepthInputValue
COUNT_HOOK(115, 824C3E58)  // addListener
COUNT_HOOK(116, 824C3ED0)  // removeListener
COUNT_HOOK(117, 824C2FA0)  // getRenderSystemEvents
COUNT_HOOK(123, 824C3F48)  // fireEvent
COUNT_HOOK(125, 82572528)  // initialiseFromRenderSystemCapabilities
COUNT_HOOK(126, 8256E848)  // initConfigOptions
}  // extern "C"

namespace torchlight::hooks {

SlotAttribution GetSlotAttribution(uint32_t slot) {
  switch (slot) {
    case 12: case 13: case 14: case 35: case 69: case 86: case 118: case 119: case 120: case 121:
      return {false, "shared empty function 0x825BC790 (1763 vtable entries); not hooked"};
    case 27: case 122:
      return {false, "constant function shared with many classes; not hooked"};
    case 60: case 71: case 73: case 110:
      return {false, "getter shared with other classes; not hooked"};
    case 18: case 19:
      return {false, "slots 18/19 share one function also used by other classes; not hooked"};
    case 103: case 105:
      return {false, "folded getter shared with other classes (state read from +0x290 at draw)"};
    case 108: case 109:
      return {false, "slots 108/109 share one function; not hooked"};
    case 1: case 11: case 62: case 126:
      return {true, "count includes direct calls from guest code"};
    case 92:
      return {true, "called by bindGpuProgramParameters for mask == GPV_PASS_ITERATION_NUMBER"};
    default:
      return {true, ""};
  }
}

}  // namespace torchlight::hooks

namespace torchlight::hooks {

void InstallGuestD3DSkip(bool native_only) {
  g_skip_guest_d3d = SkipGuestD3D(native_only, REXCVAR_GET(native_skip_guest_d3d));
  REXLOG_INFO("guest D3D: {} (--native_skip_guest_d3d={}, native mode {})",
              g_skip_guest_d3d ? "skipped for the device-only calls (sampler states, draws, bindings)"
                               : "runs in full",
              REXCVAR_GET(native_skip_guest_d3d), native_only ? "on" : "off");
}

}  // namespace torchlight::hooks
