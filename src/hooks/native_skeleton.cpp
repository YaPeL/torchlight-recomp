// OGRE's node and bone transforms as host code (--native_skeleton, off by default): Node::_update,
// Node::needUpdate, Bone::needUpdate, Node::_updateFromParent, Node::updateFromParentImpl and the
// derived orientation/position/scale getters, under Entity::cacheBoneMatrices and the scene graph.
//
// Ported from torchlight-nx 1.0.3 (https://github.com/liartes/torchlight-nx, tag v1.0.3, commit
// 6624ad2, src/torchlight_ogre_natif.cpp; GPL-3.0, after NFSMW-NX's method), which wrote them for
// the same game executable and verified them bit for bit against the recompiled code. Changes here:
// guest memory through guest_abi's readers and writers (the host offset, the store trace), guest
// functions through the SDK's resolver, the comments in English, and our two checks.
//
// The arithmetic reproduces the guest's in its order and rounding: fmuls/fadds as float operations,
// fmadds/fmsubs/fnmsubs as float(fma) in double (as the recompiler emits them), and the file is
// built without contraction so the compiler fuses nothing itself. Virtual slots are called natively
// only when they hold OGRE's Node function (SceneNode, TagPoint and Bone override some); otherwise
// the guest's function runs.
//
// Checks, in diagnostics builds: --diag_store_compare with --diag_store_compare_native compares
// each call on the render thread by the store trace (memory other than the stack, r1, and r3 for
// the getters), the guest's run free of these hooks (store_trace::t_reference_run); and
// --diag_skeleton_check is torchlight-nx's validate mode for updateFromParentImpl (the derived
// transforms bit for bit).

#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include "guest_abi/guest_functions.h"
#include "guest_abi/ogre_layout.h"
#include "hooks/native_skeleton.h"

#if TORCHLIGHT_DIAGNOSTICS
#include "diag/store_trace_api.h"
#endif

#pragma clang fp contract(off)

REXCVAR_DEFINE_BOOL(native_skeleton, false, "Torchlight",
                    "Run OGRE's node and bone transform updates as host code (ported from "
                    "torchlight-nx; false: the recompiled guest functions)");
#if TORCHLIGHT_DIAGNOSTICS
REXCVAR_DEFINE_BOOL(diag_skeleton_check, false, "Torchlight",
                    "Diagnostics: torchlight-nx's validate mode: run the guest's "
                    "updateFromParentImpl, then the host computation from the same inputs, and "
                    "count the derived transforms that differ bit for bit");
#endif

namespace {

namespace abi = torchlight::guest_abi;
namespace fn = torchlight::guest_abi::functions;

#define FUNCTION_ADDRESS_CHECK(entry, addr) \
  static_assert(fn::entry.address == 0x##addr##u, "guest_functions mismatch")

// Node (OGRE 1.7, the Xbox build): +4 mParent, +12 mChildren (a hash_map's list: next +0, Node*
// +36), +52 mChildrenToUpdate (an MSVC std::set: head +56, size +60; nodes: left +0, parent +4,
// right +8, value +12, isnil +17), +72 mNeedParentUpdate, +73 mNeedChildUpdate, +74
// mParentNotified, +104 mOrientation (w x y z), +120 mPosition, +132 mScale, +144
// mInheritOrientation, +145 mInheritScale, +148 mDerivedOrientation, +164 mDerivedPosition, +176
// mDerivedScale, +292 mCachedTransformOutOfDate, +296 mListener. Bone: +310 manually controlled,
// +312 mCreator (the Skeleton: +264 mManualBonesDirty). Vtable byte offsets: 4 _updateFromParent,
// 8 updateFromParentImpl, 188/192/196 the derived orientation/position/scale getters, 204 _update,
// 252 needUpdate, 256 requestUpdate; the Skeleton's 300 _notifyManualBoneStateChange.
constexpr uint32_t kConstants = 0x820AA2C0;  // the game's float constants: +124 is 2.0
constexpr uint32_t kUpdateFromParent = 0x821C9F08, kUpdateFromParentImpl = 0x821C9770;
constexpr uint32_t kDerivedOrientation = 0x821C9BB0, kDerivedPosition = 0x821C8518;
constexpr uint32_t kDerivedScale = 0x821C99D0, kNodeUpdate = 0x821C9F68;
constexpr uint32_t kNotifyManualBoneStateChange = 0x824E8F60;
constexpr uint32_t kSetClear = 0x8246CA80;  // the std::set's clear

uint32_t L32(const uint8_t* base, uint32_t a) { return abi::ReadU32(base, a); }
uint8_t L8(const uint8_t* base, uint32_t a) { return abi::ReadU8(base, a); }
float LF(const uint8_t* base, uint32_t a) { return abi::ReadF32(base, a); }
void S32(uint8_t* base, uint32_t a, uint32_t v) { abi::WriteU32(base, a, v); }
void S8(uint8_t* base, uint32_t a, uint8_t v) { abi::WriteBytes(base, a, &v, 1); }
void SF(uint8_t* base, uint32_t a, float f) { S32(base, a, std::bit_cast<uint32_t>(f)); }

float FmaG(float a, float b, float c) { return float(std::fma(double(a), double(b), double(c))); }
float FmsG(float a, float b, float c) { return float(std::fma(double(a), double(b), -double(c))); }
float FnmsG(float a, float b, float c) {  // fnmsubs: -(a * b - c)
  return float(-std::fma(double(a), double(b), -double(c)));
}

uint32_t Slot(const uint8_t* base, uint32_t object, uint32_t offset) {
  return L32(base, L32(base, object) + offset);
}

// A guest function by address with r3 = object, r4, r5, on the caller's stack; returns r3.
uint32_t CallGuest(PPCContext& ctx, uint8_t* base, uint32_t function, uint32_t object,
                   uint64_t r4 = 0, uint64_t r5 = 0) {
  ctx.r3.u64 = object;
  ctx.r4.u64 = r4;
  ctx.r5.u64 = r5;
  rex::runtime::ResolveIndirectFunction(function)(ctx, base);
  return ctx.r3.u32;
}

void UpdateFromParent(PPCContext& ctx, uint8_t* base, uint32_t node);

uint32_t Derived(PPCContext& ctx, uint8_t* base, uint32_t node, uint32_t slot, uint32_t native,
                 uint32_t field) {
  const uint32_t f = Slot(base, node, slot);
  if (f != native) return CallGuest(ctx, base, f, node);
  if (L8(base, node + 72)) UpdateFromParent(ctx, base, node);
  return node + field;
}

// 0x821C9BF8: Quaternion * Vector3 (q: w x y z), in the guest's order of operations.
void RotateVector(const uint8_t* base, const float q[4], const float v[3], float out[3]) {
  const float two = LF(base, kConstants + 124);
  const float w = q[0], qx = q[1], qy = q[2], qz = q[3];
  const float uvy = FmsG(v[0], qz, v[2] * qx);
  const float uvx = FmsG(v[2], qy, v[1] * qz);
  const float uvz = FmsG(v[1], qx, v[0] * qy);
  const float w2 = w * two;
  const float uuvx = FmsG(uvz, qy, uvy * qz);
  const float uuvz = FmsG(uvy, qx, qy * uvx);
  const float uuvy = FmsG(uvx, qz, uvz * qx);
  out[0] = (v[0] + uvx * w2) + uuvx * two;
  out[1] = (v[1] + uvy * w2) + uuvy * two;
  out[2] = (v[2] + uvz * w2) + uuvz * two;
}

// 0x821C9770: Node::updateFromParentImpl; the derived orientation, position and scale in out[10].
void UpdateFromParentImplValues(PPCContext& ctx, uint8_t* base, uint32_t node, float out[10]) {
  float o[4], p[3], s[3];
  for (int i = 0; i < 4; i++) o[i] = LF(base, node + 104 + 4 * i);
  for (int i = 0; i < 3; i++) p[i] = LF(base, node + 120 + 4 * i);
  for (int i = 0; i < 3; i++) s[i] = LF(base, node + 132 + 4 * i);
  const uint32_t parent = L32(base, node + 4);
  if (!parent) {
    std::memcpy(out, o, 16);
    std::memcpy(out + 4, p, 12);
    std::memcpy(out + 7, s, 12);
    return;
  }
  const uint32_t pq_addr = Derived(ctx, base, parent, 188, kDerivedOrientation, 148);
  if (L8(base, node + 144)) {
    float pq[4];
    for (int i = 0; i < 4; i++) pq[i] = LF(base, pq_addr + 4 * i);
    const float pw = pq[0], px = pq[1], py = pq[2], pz = pq[3];
    const float ow = o[0], ox = o[1], oy = o[2], oz = o[3];
    const float t1 = FmsG(ow, pw, ox * px);
    const float t2 = FmaG(oz, py, ow * px);
    const float t8 = FmaG(ow, pz, oy * px);
    const float t10 = FmaG(ox, pz, ow * py);
    const float t7 = FmaG(ox, pw, t2);
    const float t5 = FnmsG(oy, py, t1);
    const float t1b = FmaG(oz, pw, t8);
    const float t2b = FmaG(oy, pw, t10);
    out[0] = FnmsG(oz, pz, t5);
    out[1] = FnmsG(oy, pz, t7);
    out[2] = FnmsG(oz, px, t2b);
    out[3] = FnmsG(ox, py, t1b);
  } else {
    std::memcpy(out, o, 16);
  }
  const uint32_t ps_addr = Derived(ctx, base, parent, 196, kDerivedScale, 176);
  float ps[3];
  for (int i = 0; i < 3; i++) ps[i] = LF(base, ps_addr + 4 * i);
  const bool inherit_scale = L8(base, node + 145) != 0;
  for (int i = 0; i < 3; i++) out[7 + i] = inherit_scale ? s[i] * ps[i] : s[i];
  float scaled[3], pq[4];
  for (int i = 0; i < 3; i++) scaled[i] = ps[i] * p[i];
  for (int i = 0; i < 4; i++) pq[i] = LF(base, pq_addr + 4 * i);
  RotateVector(base, pq, scaled, out + 4);
  const uint32_t pp_addr = Derived(ctx, base, parent, 192, kDerivedPosition, 164);
  for (int i = 0; i < 3; i++) out[4 + i] = out[4 + i] + LF(base, pp_addr + 4 * i);
}

void UpdateFromParentImpl(PPCContext& ctx, uint8_t* base, uint32_t node) {
  float out[10];
  UpdateFromParentImplValues(ctx, base, node, out);
  for (int i = 0; i < 10; i++) SF(base, node + 148 + 4 * i, out[i]);
  S8(base, node + 292, 1);
  S8(base, node + 72, 0);
}

// 0x821C9F08: Node::_updateFromParent (updateFromParentImpl, then mListener->nodeUpdated).
void UpdateFromParent(PPCContext& ctx, uint8_t* base, uint32_t node) {
  const uint32_t f = Slot(base, node, 4);
  if (f != kUpdateFromParent) {
    CallGuest(ctx, base, f, node);
    return;
  }
  const uint32_t impl = Slot(base, node, 8);
  if (impl == kUpdateFromParentImpl) {
    UpdateFromParentImpl(ctx, base, node);
  } else {
    CallGuest(ctx, base, impl, node);
  }
  const uint32_t listener = L32(base, node + 296);
  if (listener) CallGuest(ctx, base, Slot(base, listener, 4), listener, node);
}

// 0x8246CA80 on mChildrenToUpdate: an empty set (the usual case) here, else the guest's clear.
void ClearChildrenToUpdate(PPCContext& ctx, uint8_t* base, uint32_t node) {
  const uint32_t set = node + 52, head = L32(base, set + 4);
  if (!L8(base, L32(base, head + 4) + 17)) {
    ctx.r3.u64 = set;
    rex::runtime::ResolveIndirectFunction(kSetClear)(ctx, base);
    return;
  }
  S32(base, head + 4, head);
  S32(base, head + 0, head);
  S32(base, head + 8, head);
  S32(base, set + 8, 0);
}

// 0x824C6960: the next node of an MSVC tree (isnil +17).
uint32_t SetNext(const uint8_t* base, uint32_t n) {
  if (L8(base, n + 17)) return n;
  uint32_t r = L32(base, n + 8);
  if (!L8(base, r + 17)) {
    while (!L8(base, L32(base, r) + 17)) r = L32(base, r);
    return r;
  }
  uint32_t p = L32(base, n + 4);
  while (!L8(base, p + 17) && n == L32(base, p + 8)) {
    n = p;
    p = L32(base, p + 4);
  }
  return p;
}

void NodeUpdate(PPCContext& ctx, uint8_t* base, uint32_t node, bool update_children,
                bool parent_changed);

void ChildUpdate(PPCContext& ctx, uint8_t* base, uint32_t child, bool parent_changed) {
  const uint32_t f = Slot(base, child, 204);
  if (f == kNodeUpdate) {
    NodeUpdate(ctx, base, child, true, parent_changed);
  } else {
    CallGuest(ctx, base, f, child, 1, parent_changed ? 1 : 0);
  }
}

// 0x821C9F68: Node::_update(updateChildren, parentHasChanged).
void NodeUpdate(PPCContext& ctx, uint8_t* base, uint32_t node, bool update_children,
                bool parent_changed) {
  S8(base, node + 74, 0);
  if (!update_children && !L8(base, node + 72) && !L8(base, node + 73) && !parent_changed) return;
  if (L8(base, node + 72) || parent_changed) UpdateFromParent(ctx, base, node);
  if (!L8(base, node + 73) && !parent_changed) {
    const uint32_t head = L32(base, node + 56);
    for (uint32_t it = L32(base, head); it != head; it = SetNext(base, it)) {
      ChildUpdate(ctx, base, L32(base, it + 12), false);
    }
  } else {
    const uint32_t head = L32(base, node + 12);
    for (uint32_t it = L32(base, head); it != head; it = L32(base, it)) {
      ChildUpdate(ctx, base, L32(base, it + 36), true);
    }
  }
  ClearChildrenToUpdate(ctx, base, node);
  S8(base, node + 73, 0);
}

// 0x821CA0B8: Node::needUpdate(forceParentUpdate).
void NeedUpdate(PPCContext& ctx, uint8_t* base, uint32_t node, uint64_t force) {
  S8(base, node + 72, 1);
  S8(base, node + 73, 1);
  S8(base, node + 292, 1);
  const uint32_t parent = L32(base, node + 4);
  if (parent && (!L8(base, node + 74) || uint8_t(force))) {
    CallGuest(ctx, base, Slot(base, parent, 256), parent, node, force);
    S8(base, node + 74, 1);
  }
  ClearChildrenToUpdate(ctx, base, node);
}

// 0x821CA140: Bone::needUpdate; then, for a manually controlled bone, the creator Skeleton's
// _notifyManualBoneStateChange (0x824E8F60 sets mManualBonesDirty).
void BoneNeedUpdate(PPCContext& ctx, uint8_t* base, uint32_t node, uint64_t force) {
  NeedUpdate(ctx, base, node, force);
  if (!L8(base, node + 310)) return;
  const uint32_t creator = L32(base, node + 312);
  const uint32_t f = Slot(base, creator, 300);
  if (f == kNotifyManualBoneStateChange) {
    S8(base, creator + 264, 1);
  } else {
    CallGuest(ctx, base, f, creator, node);
  }
}

bool Enabled() {
  static const bool enabled = REXCVAR_GET(native_skeleton);
  return enabled;
}

// The hooks' native bodies, as guest functions (for the comparison's candidate).
void NativeUpdateFromParentImpl(PPCContext& ctx, uint8_t* base) {
  UpdateFromParentImpl(ctx, base, ctx.r3.u32);
}
void NativeUpdateFromParent(PPCContext& ctx, uint8_t* base) {
  UpdateFromParent(ctx, base, ctx.r3.u32);
}
template <uint32_t kField>
void NativeDerived(PPCContext& ctx, uint8_t* base) {
  const uint32_t node = ctx.r3.u32;
  if (L8(base, node + 72)) UpdateFromParent(ctx, base, node);
  ctx.r3.u64 = node + kField;
}
void NativeNodeUpdate(PPCContext& ctx, uint8_t* base) {
  NodeUpdate(ctx, base, ctx.r3.u32, ctx.r4.u8 != 0, ctx.r5.u8 != 0);
}
void NativeNeedUpdate(PPCContext& ctx, uint8_t* base) {
  NeedUpdate(ctx, base, ctx.r3.u32, ctx.r4.u64);
}
void NativeBoneNeedUpdate(PPCContext& ctx, uint8_t* base) {
  BoneNeedUpdate(ctx, base, ctx.r3.u32, ctx.r4.u64);
}

#if TORCHLIGHT_DIAGNOSTICS
// --diag_skeleton_check (torchlight-nx's mode 2 for updateFromParentImpl): the guest's function
// first, then the host's values from the same inputs (the parents already updated by the guest's
// run, so computing them again has no side effect), compared bit for bit with what the guest
// stored.
void CheckedUpdateFromParentImpl(PPCContext& ctx, uint8_t* base,
                                 void (*original)(PPCContext&, uint8_t*)) {
  static std::atomic<uint64_t> checked{0}, differing{0};
  const uint32_t node = ctx.r3.u32;
  original(ctx, base);
  float out[10];
  UpdateFromParentImplValues(ctx, base, node, out);
  bool same = true;
  for (int i = 0; i < 10; i++) {
    same = same && L32(base, node + 148 + 4 * i) == std::bit_cast<uint32_t>(out[i]);
  }
  const uint64_t n = checked.fetch_add(1, std::memory_order_relaxed) + 1;
  if (!same && differing.fetch_add(1, std::memory_order_relaxed) < 20) {
    REXLOG_WARN("skeleton check: updateFromParentImpl of node {:08X} differs from the guest's",
                node);
  }
  if (n % 1000000 == 0) {
    REXLOG_INFO("skeleton check: {} updateFromParentImpl calls compared, {} differ", n,
                differing.load(std::memory_order_relaxed));
  }
}
#endif

}  // namespace

namespace torchlight::hooks {

void LogNativeSkeletonMode() {
  REXLOG_INFO("node and bone updates: {} (--native_skeleton={})",
              REXCVAR_GET(native_skeleton) ? "host code (ported from torchlight-nx)"
                                           : "the recompiled guest code",
              REXCVAR_GET(native_skeleton));
}

}  // namespace torchlight::hooks

extern "C" {

#if TORCHLIGHT_DIAGNOSTICS
#define TL_SKELETON_DIAG(addr, native, registers)                                               \
  if (torchlight::diag::store_trace::t_reference_run) return __imp__sub_##addr(ctx, base);      \
  if (torchlight::diag::store_trace::CompareNativeCall(ctx, base, 0x##addr##u, &native,         \
                                                       &__imp__sub_##addr, registers)) {        \
    return;                                                                                     \
  }
#define TL_VOID 0u
#define TL_R3 torchlight::diag::store_trace::kCompareR3
#else
#define TL_SKELETON_DIAG(addr, native, registers)
#define TL_VOID 0u
#define TL_R3 0u
#endif

#define TL_SKELETON_HOOK(entry, addr, native, registers)                                        \
  FUNCTION_ADDRESS_CHECK(entry, addr);                                                          \
  REX_EXTERN(__imp__sub_##addr);                                                                \
  REX_FUNC(sub_##addr) {                                                                        \
    if (!Enabled()) return __imp__sub_##addr(ctx, base);                                        \
    TL_SKELETON_DIAG(addr, native, registers)                                                   \
    native(ctx, base);                                                                          \
  }

FUNCTION_ADDRESS_CHECK(kNodeUpdateFromParentImpl, 821C9770);
REX_EXTERN(__imp__sub_821C9770);
REX_FUNC(sub_821C9770) {
  if (!Enabled()) return __imp__sub_821C9770(ctx, base);
#if TORCHLIGHT_DIAGNOSTICS
  if (REXCVAR_GET(diag_skeleton_check) && !torchlight::diag::store_trace::t_reference_run) {
    return CheckedUpdateFromParentImpl(ctx, base, &__imp__sub_821C9770);
  }
#endif
  TL_SKELETON_DIAG(821C9770, NativeUpdateFromParentImpl, TL_VOID)
  NativeUpdateFromParentImpl(ctx, base);
}
TL_SKELETON_HOOK(kNodeUpdateFromParent, 821C9F08, NativeUpdateFromParent, TL_VOID)
TL_SKELETON_HOOK(kNodeDerivedOrientation, 821C9BB0, NativeDerived<148>, TL_R3)
TL_SKELETON_HOOK(kNodeDerivedPosition, 821C8518, NativeDerived<164>, TL_R3)
TL_SKELETON_HOOK(kNodeDerivedScale, 821C99D0, NativeDerived<176>, TL_R3)
TL_SKELETON_HOOK(kNodeUpdate, 821C9F68, NativeNodeUpdate, TL_VOID)
TL_SKELETON_HOOK(kNodeNeedUpdate, 821CA0B8, NativeNeedUpdate, TL_VOID)
TL_SKELETON_HOOK(kBoneNeedUpdate, 821CA140, NativeBoneNeedUpdate, TL_VOID)

#undef TL_SKELETON_HOOK
#undef TL_SKELETON_DIAG
#undef TL_VOID
#undef TL_R3

}  // extern "C"
