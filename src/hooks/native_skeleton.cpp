// OGRE's node and bone transforms as host code (--native_skeleton; on by default in the Linux
// x86-64-v3 build only): Node::_update,
// Node::needUpdate, Bone::needUpdate, Node::_updateFromParent, Node::updateFromParentImpl and the
// derived orientation/position/scale getters, under Entity::cacheBoneMatrices and the scene graph.
//
// Ported from torchlight-nx 1.0.3 (https://github.com/liartes/torchlight-nx, tag v1.0.3, commit
// 6624ad2, src/torchlight_ogre_natif.cpp; GPL-3.0, after NFSMW-NX's method), which wrote them for
// the same game executable and verified them bit for bit against the recompiled code. Changes here:
// guest memory through guest_abi's readers and writers (the host offset, the store trace), guest
// functions through the image's function table as the generated code's indirect calls (else the
// SDK's resolver), the layout in guest_abi/ogre_node.h, the comments in English, and our checks.
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
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

#include <fmt/format.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

// The function table of the image (REX_LOOKUP_FUNC, REX_CODE_BASE), as the generated code
// finds indirect calls.
#include "torchlight_pch.h"

#include "guest_abi/guest_functions.h"
#include "guest_abi/ogre_layout.h"
#include "guest_abi/ogre_node.h"
#include "hooks/native_skeleton.h"

#if TORCHLIGHT_DIAGNOSTICS
#include "diag/store_trace_api.h"
#endif

#pragma clang fp contract(off)

// On by default in the Linux x86-64-v3 build only (TORCHLIGHT_NATIVE_SKELETON_DEFAULT, CMakeLists.txt;
// the owner's decision of 2026-11-01): measured there −5.9 % cycles a frame on the Steam Deck and
// −6 % on the desktop with a PGO profile trained with it on; on x86-64-v2 (Windows) its fma calls go
// to libm as the recompiled code's and it gains nothing, and on ARM64 (the POCO) no clear gain.
#ifndef TORCHLIGHT_NATIVE_SKELETON_DEFAULT
#define TORCHLIGHT_NATIVE_SKELETON_DEFAULT 0
#endif
REXCVAR_DEFINE_BOOL(native_skeleton, TORCHLIGHT_NATIVE_SKELETON_DEFAULT != 0, "Torchlight",
                    "Run OGRE's node and bone transform updates as host code (ported from "
                    "torchlight-nx; false: the recompiled guest functions)");
REXCVAR_DEFINE_BOOL(skeleton_crossing_counts, false, "Torchlight",
                    "Measurement: with --native_skeleton, count the host code's calls back into "
                    "the guest by target and time one in 64, logged every 10 s");
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

namespace node = torchlight::guest_abi::ogre_node;
namespace tree = torchlight::guest_abi::ogre::stl_tree;

// The functions the virtual slots are compared with (the native code runs only for OGRE's own).
constexpr uint32_t kUpdateFromParent = fn::kNodeUpdateFromParent.address;
constexpr uint32_t kUpdateFromParentImpl = fn::kNodeUpdateFromParentImpl.address;
constexpr uint32_t kDerivedOrientation = fn::kNodeDerivedOrientation.address;
constexpr uint32_t kDerivedPosition = fn::kNodeDerivedPosition.address;
constexpr uint32_t kDerivedScale = fn::kNodeDerivedScale.address;
constexpr uint32_t kNodeUpdate = fn::kNodeUpdate.address;
constexpr uint32_t kNotifyManualBonesDirty = fn::kSkeletonNotifyManualBonesDirty.address;

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

// --skeleton_crossing_counts: the calls back into the guest by target (a small open-addressed
// table, relaxed atomics; targets past its size counted apart), one call in 64 timed with its
// guest callee (none while an outer timed call is open, so nested calls are not counted twice),
// and the hooks' entries; logged every 10 s. All of it in cold functions, so with the option off
// CallAddress stays as small as without it.
struct Crossings {
  static constexpr size_t kSlots = 128;
  std::atomic<uint32_t> target[kSlots] = {};
  std::atomic<uint64_t> calls[kSlots] = {};
  std::atomic<uint64_t> timed{0}, timed_ns{0}, total{0}, overflow{0};
  alignas(64) std::atomic<uint64_t> entries{0};
  std::atomic<int64_t> last_log{0};
};
Crossings g_crossings;
thread_local bool t_timing = false;

bool CountCrossings() {
  static const bool count = REXCVAR_GET(skeleton_crossing_counts);
  return count;
}

[[gnu::cold, gnu::noinline]] void LogCrossings() {
  if ((g_crossings.entries.fetch_add(1, std::memory_order_relaxed) & 1023) != 0) return;
  const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now().time_since_epoch())
                          .count();
  int64_t last = g_crossings.last_log.load(std::memory_order_relaxed);
  if (last == 0) {
    g_crossings.last_log.compare_exchange_strong(last, now);
    return;
  }
  if (now - last < 10000 || !g_crossings.last_log.compare_exchange_strong(last, now)) return;
  const double seconds = double(now - last) / 1000.0;
  std::string targets;
  for (size_t i = 0; i < Crossings::kSlots; ++i) {
    const uint64_t n = g_crossings.calls[i].exchange(0, std::memory_order_acquire);
    const uint32_t target = g_crossings.target[i].load(std::memory_order_relaxed);
    if (!n || !target) continue;
    targets += fmt::format(" {:08X} {:.0f}/s", target, double(n) / seconds);
  }
  const uint64_t timed = g_crossings.timed.exchange(0), ns = g_crossings.timed_ns.exchange(0);
  REXLOG_INFO("skeleton crossings: {:.0f} hook entries/s, {:.0f} guest calls/s ({} past the "
              "table), {:.0f} ns a call (callee included, {} timed); by target:{}",
              double(g_crossings.entries.exchange(0)) / seconds,
              double(g_crossings.total.exchange(0)) / seconds, g_crossings.overflow.exchange(0),
              timed ? double(ns) / double(timed) : 0.0, timed, targets);
}

[[gnu::cold, gnu::noinline]] void CountedCall(PPCContext& ctx, uint8_t* base, PPCFunc* f,
                                              uint32_t function) {
  size_t i = (function >> 2) % Crossings::kSlots;
  bool counted = false;
  for (size_t probe = 0; probe < Crossings::kSlots; ++probe, i = (i + 1) % Crossings::kSlots) {
    uint32_t seen = g_crossings.target[i].load(std::memory_order_relaxed);
    if (seen == 0 && !g_crossings.target[i].compare_exchange_strong(seen, function)) {
      // Another thread took the slot: `seen` is its target.
    } else if (seen == 0) {
      seen = function;
    }
    if (seen == function) {
      g_crossings.calls[i].fetch_add(1, std::memory_order_release);
      counted = true;
      break;
    }
  }
  if (!counted) g_crossings.overflow.fetch_add(1, std::memory_order_relaxed);
  const uint64_t n = g_crossings.total.fetch_add(1, std::memory_order_relaxed);
  if ((n & 63) != 0 || t_timing) {
    f(ctx, base);
    return;
  }
  t_timing = true;
  const auto start = std::chrono::steady_clock::now();
  f(ctx, base);
  g_crossings.timed_ns.fetch_add(uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                              std::chrono::steady_clock::now() - start)
                                              .count()),
                                 std::memory_order_relaxed);
  g_crossings.timed.fetch_add(1, std::memory_order_relaxed);
  t_timing = false;
}

// A guest function by address, as the generated code's indirect calls find it: the image's
// function table (REX_LOOKUP_FUNC), else the SDK's resolver.
void CallAddress(PPCContext& ctx, uint8_t* base, uint32_t function) {
  PPCFunc* f = nullptr;
  if (uint32_t(function - REX_CODE_BASE) < REX_CODE_SIZE + REX_THUNK_RESERVE_SIZE) {
    f = REX_LOOKUP_FUNC(base, function);
  }
  if (!f) {
    ctx.last_indirect_target = function;
    f = rex::runtime::ResolveIndirectFunction(function);
  }
  if (CountCrossings()) [[unlikely]] return CountedCall(ctx, base, f, function);
  f(ctx, base);
}

// A guest function with r3 = object, r4, r5, on the caller's stack; returns r3.
uint32_t CallGuest(PPCContext& ctx, uint8_t* base, uint32_t function, uint32_t object,
                   uint64_t r4 = 0, uint64_t r5 = 0) {
  ctx.r3.u64 = object;
  ctx.r4.u64 = r4;
  ctx.r5.u64 = r5;
  CallAddress(ctx, base, function);
  return ctx.r3.u32;
}

uint32_t At(uint32_t object, abi::Field f) { return object + f.offset; }

void UpdateFromParent(PPCContext& ctx, uint8_t* base, uint32_t n);

uint32_t Derived(PPCContext& ctx, uint8_t* base, uint32_t n, uint32_t slot, uint32_t native,
                 abi::Field field) {
  const uint32_t f = Slot(base, n, slot);
  if (f != native) return CallGuest(ctx, base, f, n);
  if (L8(base, At(n, node::kNeedParentUpdate))) UpdateFromParent(ctx, base, n);
  return At(n, field);
}

// 0x821C9BF8: Quaternion * Vector3 (q: w x y z), in the guest's order of operations.
void RotateVector(const uint8_t* base, const float q[4], const float v[3], float out[3]) {
  const float two = LF(base, node::kFloatTwo);
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

void LoadFloats(const uint8_t* base, uint32_t address, float* v, int count) {
  for (int i = 0; i < count; i++) v[i] = LF(base, address + 4 * i);
}

// 0x821C9770: Node::updateFromParentImpl; the derived orientation, position and scale in out[10].
// Each field is read where the guest reads it (after the parent getter before it), the parent
// again before each getter, and the guest's scalar FPU mode (flush to zero off) set before each
// block of arithmetic, after any guest call that may have changed it.
void UpdateFromParentImplValues(PPCContext& ctx, uint8_t* base, uint32_t n, float out[10]) {
  uint32_t parent = L32(base, At(n, node::kParent));
  if (!parent) {
    ctx.fpscr.disableFlushMode();  // as the guest's path (0x821C9958), for the mode it leaves
    LoadFloats(base, At(n, node::kOrientation), out, 4);
    LoadFloats(base, At(n, node::kPosition), out + 4, 3);
    LoadFloats(base, At(n, node::kScale), out + 7, 3);
    return;
  }
  const uint32_t pq_addr = Derived(ctx, base, parent, node::kVtDerivedOrientation,
                                   kDerivedOrientation, node::kDerivedOrientation);
  float o[4];
  LoadFloats(base, At(n, node::kOrientation), o, 4);
  ctx.fpscr.disableFlushMode();
  if (L8(base, At(n, node::kInheritOrientation))) {
    float pq[4];
    LoadFloats(base, pq_addr, pq, 4);
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
    for (int i = 0; i < 4; i++) out[i] = o[i];
  }
  parent = L32(base, At(n, node::kParent));
  const uint32_t ps_addr =
      Derived(ctx, base, parent, node::kVtDerivedScale, kDerivedScale, node::kDerivedScale);
  float p[3], s[3], ps[3];
  LoadFloats(base, At(n, node::kPosition), p, 3);
  LoadFloats(base, At(n, node::kScale), s, 3);
  LoadFloats(base, ps_addr, ps, 3);
  ctx.fpscr.disableFlushMode();
  const bool inherit_scale = L8(base, At(n, node::kInheritScale)) != 0;
  for (int i = 0; i < 3; i++) out[7 + i] = inherit_scale ? s[i] * ps[i] : s[i];
  float scaled[3], pq[4];
  for (int i = 0; i < 3; i++) scaled[i] = ps[i] * p[i];
  LoadFloats(base, pq_addr, pq, 4);
  RotateVector(base, pq, scaled, out + 4);
  parent = L32(base, At(n, node::kParent));
  const uint32_t pp_addr = Derived(ctx, base, parent, node::kVtDerivedPosition, kDerivedPosition,
                                   node::kDerivedPosition);
  ctx.fpscr.disableFlushMode();
  for (int i = 0; i < 3; i++) out[4 + i] = out[4 + i] + LF(base, pp_addr + 4 * i);
}

void UpdateFromParentImpl(PPCContext& ctx, uint8_t* base, uint32_t n) {
  float out[10];
  UpdateFromParentImplValues(ctx, base, n, out);
  // The derived orientation, position and scale are consecutive (ogre_node.h).
  for (int i = 0; i < 10; i++) SF(base, At(n, node::kDerivedOrientation) + 4 * i, out[i]);
  S8(base, At(n, node::kCachedTransformOutOfDate), 1);
  S8(base, At(n, node::kNeedParentUpdate), 0);
}

// 0x821C9F08: Node::_updateFromParent (updateFromParentImpl, then mListener->nodeUpdated).
void UpdateFromParent(PPCContext& ctx, uint8_t* base, uint32_t n) {
  const uint32_t f = Slot(base, n, node::kVtUpdateFromParent);
  if (f != kUpdateFromParent) {
    CallGuest(ctx, base, f, n);
    return;
  }
  const uint32_t impl = Slot(base, n, node::kVtUpdateFromParentImpl);
  if (impl == kUpdateFromParentImpl) {
    UpdateFromParentImpl(ctx, base, n);
  } else {
    CallGuest(ctx, base, impl, n);
  }
  const uint32_t listener = L32(base, At(n, node::kListener));
  if (listener) {
    CallGuest(ctx, base, Slot(base, listener, node::kVtListenerNodeUpdated), listener, n);
  }
}

// 0x8246CA80 on mChildrenToUpdate: an empty set (the usual case) here, else the guest's clear.
void ClearChildrenToUpdate(PPCContext& ctx, uint8_t* base, uint32_t n) {
  const uint32_t set = At(n, node::kChildrenToUpdate);
  const uint32_t head = L32(base, At(set, tree::kHead));
  if (!L8(base, At(L32(base, At(head, tree::kNodeParent)), tree::kSetNodeIsNil))) {
    ctx.r3.u64 = set;
    CallAddress(ctx, base, fn::kSetClear.address);
    return;
  }
  S32(base, At(head, tree::kNodeParent), head);
  S32(base, At(head, tree::kNodeLeft), head);
  S32(base, At(head, tree::kNodeRight), head);
  S32(base, At(set, tree::kSize), 0);
}

// 0x824C6960: the next node of a set<ptr> (isnil +0x11).
uint32_t SetNext(const uint8_t* base, uint32_t n) {
  auto nil = [&](uint32_t x) { return L8(base, At(x, tree::kSetNodeIsNil)) != 0; };
  if (nil(n)) return n;
  uint32_t r = L32(base, At(n, tree::kNodeRight));
  if (!nil(r)) {
    while (!nil(L32(base, At(r, tree::kNodeLeft)))) r = L32(base, At(r, tree::kNodeLeft));
    return r;
  }
  uint32_t p = L32(base, At(n, tree::kNodeParent));
  while (!nil(p) && n == L32(base, At(p, tree::kNodeRight))) {
    n = p;
    p = L32(base, At(p, tree::kNodeParent));
  }
  return p;
}

void NodeUpdate(PPCContext& ctx, uint8_t* base, uint32_t n, bool update_children,
                bool parent_changed);

void ChildUpdate(PPCContext& ctx, uint8_t* base, uint32_t child, bool parent_changed) {
  const uint32_t f = Slot(base, child, node::kVtUpdate);
  if (f == kNodeUpdate) {
    NodeUpdate(ctx, base, child, true, parent_changed);
  } else {
    CallGuest(ctx, base, f, child, 1, parent_changed ? 1 : 0);
  }
}

// 0x821C9F68: Node::_update(updateChildren, parentHasChanged).
void NodeUpdate(PPCContext& ctx, uint8_t* base, uint32_t n, bool update_children,
                bool parent_changed) {
  const uint32_t need_parent = At(n, node::kNeedParentUpdate);
  const uint32_t need_child = At(n, node::kNeedChildUpdate);
  S8(base, At(n, node::kParentNotified), 0);
  if (!update_children && !L8(base, need_parent) && !L8(base, need_child) && !parent_changed) {
    return;
  }
  if (L8(base, need_parent) || parent_changed) UpdateFromParent(ctx, base, n);
  if (!L8(base, need_child) && !parent_changed) {
    const uint32_t head = L32(base, At(At(n, node::kChildrenToUpdate), tree::kHead));
    for (uint32_t it = L32(base, At(head, tree::kNodeLeft)); it != head; it = SetNext(base, it)) {
      ChildUpdate(ctx, base, L32(base, At(it, tree::kNodeKey)), false);
    }
  } else {
    const uint32_t head = L32(base, At(n, node::kChildren));
    for (uint32_t it = L32(base, At(head, node::kChildListNext)); it != head;
         it = L32(base, At(it, node::kChildListNext))) {
      ChildUpdate(ctx, base, L32(base, At(it, node::kChildListNode)), true);
    }
  }
  ClearChildrenToUpdate(ctx, base, n);
  S8(base, need_child, 0);
}

// 0x821CA0B8: Node::needUpdate(forceParentUpdate).
void NeedUpdate(PPCContext& ctx, uint8_t* base, uint32_t n, uint64_t force) {
  S8(base, At(n, node::kNeedParentUpdate), 1);
  S8(base, At(n, node::kNeedChildUpdate), 1);
  S8(base, At(n, node::kCachedTransformOutOfDate), 1);
  const uint32_t parent = L32(base, At(n, node::kParent));
  if (parent && (!L8(base, At(n, node::kParentNotified)) || uint8_t(force))) {
    CallGuest(ctx, base, Slot(base, parent, node::kVtRequestUpdate), parent, n, force);
    S8(base, At(n, node::kParentNotified), 1);
  }
  ClearChildrenToUpdate(ctx, base, n);
}

// 0x821CA140: Bone::needUpdate; then, for a manually controlled bone, its Skeleton's
// _notifyManualBonesDirty (OGRE's 0x824E8F60 sets mManualBonesDirty and takes no argument; the
// guest's r4 there is whatever the set clear left, an override gets the bone).
void BoneNeedUpdate(PPCContext& ctx, uint8_t* base, uint32_t n, uint64_t force) {
  NeedUpdate(ctx, base, n, force);
  if (!L8(base, At(n, node::kBoneManuallyControlled))) return;
  const uint32_t creator = L32(base, At(n, node::kBoneCreator));
  const uint32_t f = Slot(base, creator, node::kVtSkeletonNotifyManualBonesDirty);
  if (f == kNotifyManualBonesDirty) {
    S8(base, At(creator, node::kSkeletonManualBonesDirty), 1);
  } else {
    CallGuest(ctx, base, f, creator, n);
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
template <const abi::Field& kField>
void NativeDerived(PPCContext& ctx, uint8_t* base) {
  const uint32_t n = ctx.r3.u32;
  if (L8(base, At(n, node::kNeedParentUpdate))) UpdateFromParent(ctx, base, n);
  ctx.r3.u64 = At(n, kField);
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
  const uint32_t n = ctx.r3.u32;
  original(ctx, base);
  float out[10];
  UpdateFromParentImplValues(ctx, base, n, out);
  bool same = true;
  for (int i = 0; i < 10; i++) {
    same = same && L32(base, At(n, node::kDerivedOrientation) + 4 * i) ==
                       std::bit_cast<uint32_t>(out[i]);
  }
  const uint64_t count = checked.fetch_add(1, std::memory_order_relaxed) + 1;
  if (!same && differing.fetch_add(1, std::memory_order_relaxed) < 20) {
    REXLOG_WARN("skeleton check: updateFromParentImpl of node {:08X} differs from the guest's",
                n);
  }
  if (count % 1000000 == 0) {
    REXLOG_INFO("skeleton check: {} updateFromParentImpl calls compared, {} differ", count,
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
    if (CountCrossings()) [[unlikely]] LogCrossings();                                          \
    native(ctx, base);                                                                          \
  }

FUNCTION_ADDRESS_CHECK(kNodeUpdateFromParentImpl, 821C9770);
REX_EXTERN(__imp__sub_821C9770);
REX_FUNC(sub_821C9770) {
  if (!Enabled()) return __imp__sub_821C9770(ctx, base);
  if (CountCrossings()) [[unlikely]] LogCrossings();
#if TORCHLIGHT_DIAGNOSTICS
  static const bool check = REXCVAR_GET(diag_skeleton_check);
  if (check && !torchlight::diag::store_trace::t_reference_run) {
    return CheckedUpdateFromParentImpl(ctx, base, &__imp__sub_821C9770);
  }
#endif
  TL_SKELETON_DIAG(821C9770, NativeUpdateFromParentImpl, TL_VOID)
  NativeUpdateFromParentImpl(ctx, base);
}
TL_SKELETON_HOOK(kNodeUpdateFromParent, 821C9F08, NativeUpdateFromParent, TL_VOID)
TL_SKELETON_HOOK(kNodeDerivedOrientation, 821C9BB0, NativeDerived<node::kDerivedOrientation>, TL_R3)
TL_SKELETON_HOOK(kNodeDerivedPosition, 821C8518, NativeDerived<node::kDerivedPosition>, TL_R3)
TL_SKELETON_HOOK(kNodeDerivedScale, 821C99D0, NativeDerived<node::kDerivedScale>, TL_R3)
TL_SKELETON_HOOK(kNodeUpdate, 821C9F68, NativeNodeUpdate, TL_VOID)
TL_SKELETON_HOOK(kNodeNeedUpdate, 821CA0B8, NativeNeedUpdate, TL_VOID)
TL_SKELETON_HOOK(kBoneNeedUpdate, 821CA140, NativeBoneNeedUpdate, TL_VOID)

#undef TL_SKELETON_HOOK
#undef TL_SKELETON_DIAG
#undef TL_VOID
#undef TL_R3

}  // extern "C"
