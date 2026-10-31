// SceneManager::renderSingleObject (guest_abi/render_single_object.h) for the main scene's
// programmable objects, on the host: option B's stage 1 (docs/perf/native-render-queue.md section
// 4). Off by default (--native_render_object) until the owner's integration OK.
//
// The guest keeps the queue walk and every _setPass; this replaces the per-object function. This
// first piece is renderSingleObject's own glue: the calls it makes are the guest's, by vtable (so
// the hooks under them record the same commands), with the same arguments in the same order; the
// pure ones whose results are fixed by the checks below (getPassTransformStates and
// getPassSurfaceAndLightStates return false for every D3D9 program, setNormaliseNormals is empty,
// isProgrammable) are not called, and the texture effect scan is host reads. Later pieces replace the callees.
//
// An object takes the native path only when every branch renderSingleObject would take is one
// transcribed here: light iteration without iterating per light (the renderable's lights, or the
// static list refilled from them as the guest does), state changes not suppressed, no
// camera-relative rendering, a vertex program of a D3D9 class, no depth bias per iteration, no
// scissoring or clip planes, the known scene manager, render system and pass classes, and the
// static light list already built. Any renderable class: its virtuals are called. Anything else
// runs the guest's function; the reasons are counted and logged with every 10 s summary.
//
// In diagnostics builds the store trace's comparison (--diag_store_compare) runs this as the
// candidate against the guest's function for every call (diag/store_trace.cpp).

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include "guest_abi/guest_functions.h"
#include "guest_abi/ogre_layout.h"
#include "guest_abi/render_queue.h"
#include "guest_abi/render_single_object.h"
#include "hooks/guest_copy.h"
#include "live/frame_timing.h"
#if TORCHLIGHT_DIAGNOSTICS
#include "diag/store_trace_api.h"
#endif

REXCVAR_DEFINE_BOOL(native_render_object, false, "Torchlight",
                    "Option B stage 1: renderSingleObject on the host for the main scene's "
                    "programmable objects (hooks/native_render_object.cpp); off: the guest's");
REXCVAR_DEFINE_BOOL(native_vector_copy, false, "Torchlight",
                    "Option B stage 1: the XDK vector copy the constant writers use as a host "
                    "memcpy where that is the same copy (hooks/native_render_object.cpp); off: the "
                    "guest's");
#if TORCHLIGHT_DIAGNOSTICS
REXCVAR_DEFINE_BOOL(diag_vector_copy_check, false, "Torchlight",
                    "Diagnostics: with --native_vector_copy, run every routed copy both ways (the "
                    "guest's, then the host's over the restored destination) and log differences");
#endif

namespace {

namespace abi = torchlight::guest_abi;
namespace rq = torchlight::guest_abi::render_queue;
namespace rso = torchlight::guest_abi::render_single_object;

// Why an object did not take the native path (or that it did), counted per summary window.
enum Reason : uint32_t {
  kNative,
  kNoLightIteration,
  kSuppressed,
  kCameraRelative,
  kSceneManagerClass,
  kRenderSystemClass,
  kPassClass,
  kNoVertexProgram,
  kProgramClass,
  kIteratePerLight,
  kFirstLightIteration,
  kDepthBiasPerIteration,
  kScissorOrClip,
  kReasonCount
};
constexpr const char* kReasonNames[kReasonCount] = {
    "native",         "no light iteration", "suppressed",     "camera relative",
    "scene manager",  "render system",      "pass class",
    "no vertex program", "program class",   "per light",      "first light iteration",
    "depth bias per iteration", "scissor or clip planes"};
std::array<std::atomic<uint64_t>, kReasonCount> g_reasons{};
// _updateAutoParams (P1): calls on the host, calls left to the guest, and the first automatic
// constant type that sent a call to the guest (render thread).
std::atomic<uint64_t> g_auto_native{0}, g_auto_guest{0};
std::array<std::atomic<uint64_t>, 128> g_auto_guest_types{};
bool Enabled() {
  static const bool enabled = REXCVAR_GET(native_render_object);
  return enabled;
}

// The guest functions behind the virtual calls, resolved once each. Render thread
// only, as renderSingleObject is (unsynchronized statics). The vtables are checked against known
// classes first, so every target is a guest function.
PPCFunc* Resolve(uint32_t target) {
  // Direct-mapped by the target's address (functions are 4-byte aligned): one compare per call,
  // as the generated code's own table lookup costs; a collision only resolves again. Each slot is
  // one pair, written whole.
  struct Slot {
    uint32_t target;
    PPCFunc* function;
  };
  static Slot slots[1024];
  Slot& slot = slots[(target >> 2) & 1023];
  if (slot.target == target) return slot.function;
  PPCFunc* f = rex::runtime::ResolveIndirectFunction(target);
  if (f) slot = {target, f};
  return f;
}

// Calls `object`'s virtual `slot` with `object` in r3 and `args` in r4.. (float arguments are set
// in the context by the caller); returns r3.
template <typename... Args>
uint32_t Virtual(PPCContext& ctx, uint8_t* base, uint32_t object, abi::VtableSlot slot,
                 Args... args) {
  ctx.r3.u64 = object;
  PPCRegister* const registers[] = {&ctx.r4, &ctx.r5, &ctx.r6, &ctx.r7, &ctx.r8};
  size_t i = 0;
  ((registers[i++]->u64 = uint32_t(args)), ...);
  Resolve(abi::ReadVirtual(base, object, slot))(ctx, base);
  return ctx.r3.u32;
}

// The vtables are in the image: their slots are checked once.
bool KnownSceneManagerVtable(const uint8_t* base) {
  namespace sm = rso::scene_manager;
  static const bool known =
      abi::ReadU32(base, sm::kOctreeVtable + sm::kUseRenderableViewProjMode.byte_offset()) ==
          sm::kUseRenderableViewProjModeTarget &&
      abi::ReadU32(base, sm::kOctreeVtable + sm::kResetViewProjMode.byte_offset()) ==
          sm::kResetViewProjModeTarget &&
      abi::ReadU32(base, sm::kOctreeVtable + sm::kFireRenderSingleObject.byte_offset()) ==
          sm::kFireRenderSingleObjectTarget &&
      abi::ReadU32(base, sm::kOctreeVtable + sm::kUseLightsGpuProgram.byte_offset()) ==
          sm::kUseLightsGpuProgramTarget &&
      abi::ReadU32(base, sm::kOctreeVtable + sm::kUpdateGpuProgramParameters.byte_offset()) ==
          sm::kUpdateGpuProgramParametersTarget;
  return known;
}

bool KnownRenderSystemVtable(const uint8_t* base) {
  namespace rs = rso::render_system;
  static const bool known =
      abi::ReadU32(base, rs::kD3D9Vtable + rs::kSetNormaliseNormals.byte_offset()) ==
      rs::kEmptyFunction;
  return known;
}

// TextureUnitState::hasViewRelativeTextureCoordinateGeneration as renderSingleObject inlines it
// (guest_abi/render_single_object.h, texture_unit_state).
uint32_t FindEffect(const uint8_t* base, uint32_t head, int32_t key) {
  namespace tus = rso::texture_unit_state;
  uint32_t candidate = head;
  uint32_t node = abi::ReadU32(base, head + tus::kNodeParent);
  while (!abi::ReadU8(base, node + tus::kNodeIsNil)) {
    if (int32_t(abi::ReadU32(base, node + tus::kNodeKey)) < key) {
      node = abi::ReadU32(base, node + tus::kNodeRight);
    } else {
      candidate = node;
      node = abi::ReadU32(base, node + tus::kNodeLeft);
    }
  }
  if (candidate == head || key < int32_t(abi::ReadU32(base, candidate + tus::kNodeKey))) return head;
  return candidate;
}

uint32_t NextEffect(const uint8_t* base, uint32_t node) {
  namespace tus = rso::texture_unit_state;
  if (abi::ReadU8(base, node + tus::kNodeIsNil)) return node;
  const uint32_t right = abi::ReadU32(base, node + tus::kNodeRight);
  if (!abi::ReadU8(base, right + tus::kNodeIsNil)) {
    uint32_t next = right;
    for (uint32_t left = abi::ReadU32(base, next + tus::kNodeLeft);
         !abi::ReadU8(base, left + tus::kNodeIsNil); left = abi::ReadU32(base, next + tus::kNodeLeft))
      next = left;
    return next;
  }
  uint32_t parent = abi::ReadU32(base, node + tus::kNodeParent);
  while (!abi::ReadU8(base, parent + tus::kNodeIsNil) &&
         node == abi::ReadU32(base, parent + tus::kNodeRight)) {
    node = parent;
    parent = abi::ReadU32(base, node + tus::kNodeParent);
  }
  return parent;
}

bool ViewRelativeTexgen(const uint8_t* base, uint32_t unit) {
  namespace tus = rso::texture_unit_state;
  const uint32_t head = abi::ReadU32(base, unit + tus::kEffectsHead.offset);
  for (uint32_t node = FindEffect(base, head, tus::kEnvironmentMap); node != head;
       node = NextEffect(base, node)) {
    if (int32_t(abi::ReadU32(base, node + tus::kNodeSubtype)) == tus::kEnvReflection) return true;
  }
  return FindEffect(base, head, tus::kProjectiveTexture) != head;
}

Reason Check(const uint8_t* base, const PPCContext& ctx) {
  namespace sm = rso::scene_manager;
  const uint32_t scene = ctx.r3.u32, rend = ctx.r4.u32, pass = ctx.r5.u32;
  if ((ctx.r7.u32 & 0xFF) == 0) return kNoLightIteration;
  if (abi::ReadU8(base, scene + sm::kSuppressRenderStateChanges.offset)) return kSuppressed;
  if (abi::ReadU8(base, scene + sm::kCameraRelativeRendering.offset)) return kCameraRelative;
  if (abi::ReadU32(base, scene) != sm::kOctreeVtable || !KnownSceneManagerVtable(base))
    return kSceneManagerClass;
  const uint32_t rs = abi::ReadU32(base, scene + sm::kRenderSystem.offset);
  if (abi::ReadU32(base, rs) != rso::render_system::kD3D9Vtable || !KnownRenderSystemVtable(base))
    return kRenderSystemClass;
  if (abi::ReadU32(base, pass) != rso::pass::kVtable) return kPassClass;
  const uint32_t usage = abi::ReadU32(base, pass + rq::pass::kVertexProgramUsage.offset);
  const uint32_t program =
      usage ? abi::ReadU32(base, usage + rso::gpu_program::kUsageProgram.offset) : 0;
  if (!program) return kNoVertexProgram;
  if (abi::ReadVirtual(base, program, rso::gpu_program::kGetPassTransformStates) !=
          rso::gpu_program::kReturnFalse ||
      abi::ReadVirtual(base, program, rso::gpu_program::kGetPassSurfaceAndLightStates) !=
          rso::gpu_program::kReturnFalse)
    return kProgramClass;
  if (abi::ReadU8(base, pass + rq::pass::kIteratePerLight.offset)) return kIteratePerLight;
  // The light iteration builds the static light list on its first use (any object): the guest's.
  if (!(abi::ReadU32(base, rso::light_list::kLocalLightListGuard) & 1)) return kFirstLightIteration;
  if (abi::ReadF32(base, pass + rq::pass::kDepthBiasPerIteration.offset) != 0.0f)
    return kDepthBiasPerIteration;
  if ((ctx.r6.u32 & 0xFF) && (abi::ReadU8(base, pass + rq::pass::kLightScissoring.offset) ||
                              abi::ReadU8(base, pass + rq::pass::kLightClipPlanes.offset)))
    return kScissorOrClip;
  return kNative;
}

}  // namespace

extern "C" {
REX_EXTERN(__imp__sub_821C5FA0);
REX_EXTERN(__imp__sub_821C28D8);
REX_EXTERN(sub_8244EA10);
}

namespace {

// Matrix4::determinant(m) < 0 as renderSingleObject computes it inline (@0x821C6330..0x821C6398):
// the guest's MINOR helper for the four cofactors, the same single-precision fused steps.
bool NegativeDeterminant(PPCContext& ctx, uint8_t* base, uint32_t m, double zero) {
  auto minor = [&](uint32_t c0, uint32_t c1, uint32_t c2) {
    ctx.r3.u64 = m;
    ctx.r4.u64 = c0;
    ctx.r5.u64 = c1;
    ctx.r6.u64 = c2;
    sub_8244EA10(ctx, base);
    return ctx.f1.f64;
  };
  auto at = [&](uint32_t offset) { return double(abi::ReadF32(base, m + offset)); };
  ctx.fpscr.disableFlushMode();
  const double f29 = minor(1, 2, 3);
  const double m00 = at(0);
  const double f28 = double(float(minor(0, 2, 3) * at(4)));
  const double f1c = minor(0, 1, 3);
  const double f13 = double(float(std::fma(f29, m00, -f28)));
  const double f30 = double(float(std::fma(f1c, at(8), f13)));
  const double f1d = minor(0, 1, 2);
  const double det = double(float(-std::fma(f1d, at(12), -f30)));
  return det < zero;
}

void Native(PPCContext& ctx, uint8_t* base) {
  namespace sm = rso::scene_manager;
  namespace rs = rso::render_system;
  namespace rd = rso::renderable;
  namespace ro = rso::render_operation;
  const uint32_t scene = ctx.r3.u32, rend = ctx.r4.u32, pass = ctx.r5.u32;
  const uint32_t render_system = abi::ReadU32(base, scene + sm::kRenderSystem.offset);
  static const double zero = double(abi::ReadF32(base, rso::kZeroFloat));

  // The guest's own frame, with the RenderOperation where it keeps it (above the parameter home
  // area the callees may write).
  const uint32_t caller_sp = ctx.r1.u32;
  const uint32_t frame = caller_sp - rso::kFrameSize;
  const uint32_t op = frame + rso::kRenderOperationInFrame;
  abi::WriteU32(base, frame, caller_sp);  // the back chain, as stwu writes it
  ctx.r1.u64 = frame;
  abi::WriteU32(base, op + ro::kVertexData.offset, 0);
  abi::WriteU32(base, op + ro::kOperationType.offset, ro::kTriangleList);
  const uint8_t one = 1;
  abi::WriteBytes(base, op + ro::kUseIndexes.offset, &one, 1);
  abi::WriteU32(base, op + ro::kIndexData.offset, 0);
  abi::WriteU32(base, op + ro::kSrcRenderable.offset, 0);
  abi::WriteU32(base, op + ro::kInstanceCount.offset, 1);
  Virtual(ctx, base, rend, rd::kGetRenderOperation, op);
  abi::WriteU32(base, op + ro::kSrcRenderable.offset, rend);

  // World transforms: to mTempXform only (no program passes them to the render system).
  const uint32_t matrices = Virtual(ctx, base, rend, rd::kGetNumWorldTransforms) & 0xFFFF;
  const uint32_t temp_xform = scene + sm::kTempXform.offset;
  if (matrices > 0) Virtual(ctx, base, rend, rd::kGetWorldTransforms, temp_xform);
  Virtual(ctx, base, scene, sm::kUseRenderableViewProjMode, rend, 0);
  abi::WriteU16(base, scene + sm::kGpuParamsDirty.offset,
                abi::ReadU16(base, scene + sm::kGpuParamsDirty.offset) | sm::kGpvPerObject);
  const uint32_t source = abi::ReadU32(base, scene + sm::kAutoParamDataSource.offset);
  Virtual(ctx, base, source, rso::auto_param_data_source::kSetCurrentRenderable, rend);
  Virtual(ctx, base, source, rso::auto_param_data_source::kSetWorldMatrices, temp_xform, matrices);

  // Texture units whose coordinates depend on the view matrix are set again.
  const uint32_t units = abi::ReadU32(base, pass + rq::pass::kTextureUnitStates.offset);
  const uint32_t units_end = abi::ReadU32(base, pass + rq::pass::kTextureUnitStates.offset + 4);
  for (uint32_t it = units, index = 0; it != units_end; it += 4, ++index) {
    const uint32_t unit = abi::ReadU32(base, it);
    if (ViewRelativeTexgen(base, unit))
      Virtual(ctx, base, render_system, rs::kSetTextureUnitSettings, index, unit);
  }

  // Negative scaling flips the pass's culling mode.
  if (abi::ReadU8(base, scene + sm::kFlipCullingOnNegativeScale.offset)) {
    uint32_t cull = abi::ReadU32(base, scene + sm::kPassCullingMode.offset);
    if (NegativeDeterminant(ctx, base, temp_xform, zero)) {
      if (cull == rso::kCullClockwise) {
        cull = rso::kCullAnticlockwise;
      } else if (cull == rso::kCullAnticlockwise) {
        cull = rso::kCullClockwise;
      }
    }
    if (cull != Virtual(ctx, base, render_system, rs::kGetCullingMode))
      Virtual(ctx, base, render_system, rs::kSetCullingMode, cull);
  }

  // Polygon mode: the camera's only lowers it, when both the pass and the renderable allow it.
  uint32_t mode = abi::ReadU32(base, pass + rso::pass::kPolygonMode.offset);
  if ((Virtual(ctx, base, pass, rso::pass::kGetPolygonModeOverrideable) & 0xFF) &&
      (Virtual(ctx, base, rend, rd::kGetPolygonModeOverrideable) & 0xFF)) {
    const uint32_t camera = abi::ReadU32(base, scene + sm::kCameraInProgress.offset);
    const uint32_t camera_mode = abi::ReadU32(base, camera + rso::camera::kPolygonMode.offset);
    if (int32_t(mode) > int32_t(camera_mode)) mode = camera_mode;
  }
  Virtual(ctx, base, render_system, rs::kSetPolygonMode, mode);

  // One light iteration: the renderable's lights, or from the start light at most the pass's
  // maximum, copied into the static list.
  uint32_t lights = Virtual(ctx, base, rend, rd::kGetLights);
  const uint32_t start = abi::ReadU16(base, pass + rso::pass::kStartLight.offset);
  const uint32_t max = abi::ReadU16(base, pass + rso::pass::kMaxSimultaneousLights.offset);
  if (start != 0 || max != rso::pass::kMaxLights) {
    namespace ll = rso::light_list;
    const auto size = [&] {
      return uint32_t(int32_t(abi::ReadU32(base, lights + ll::kEnd.offset) -
                              abi::ReadU32(base, lights + ll::kBegin.offset)) >> 2);
    };
    if (start != 0 && start >= size()) {  // past the renderable's lights: nothing drawn
      Virtual(ctx, base, scene, sm::kResetViewProjMode, 0);
      ctx.r1.u64 = caller_sp;
      return;
    }
    const uint32_t local = ll::kLocalLightList;
    ctx.r3.u64 = local;
    Resolve(ll::kClear)(ctx, base);
    const uint32_t begin = abi::ReadU32(base, lights + ll::kBegin.offset);
    const uint32_t bytes = abi::ReadU32(base, lights + ll::kEnd.offset) - begin;
    const uint32_t left = uint32_t(int32_t(bytes) >> 2) - start;
    const uint32_t count = left < max ? left : max;
    const uint8_t dirty = 1;
    abi::WriteBytes(base, local + ll::kDirty.offset, &dirty, 1);
    // The registers as the guest leaves them for the insert (@0x821C6660..0x821C66B0).
    ctx.r3.u64 = abi::ReadU32(base, local + ll::kBegin.offset);
    ctx.r4.u64 = begin + (start << 2);
    ctx.r5.u64 = begin + (start << 2) + (count << 2);
    ctx.r6.s64 = int32_t(bytes) >> 2;
    ctx.r7.u64 = bytes;
    ctx.r8.u64 = start;
    ctx.r9.u64 = start << 2;
    ctx.r10.u64 = count;
    ctx.r11.u64 = count << 2;
    Resolve(ll::kInsert)(ctx, base);
    abi::WriteBytes(base, local + ll::kDirty.offset, &dirty, 1);
    lights = local;
  }
  Virtual(ctx, base, scene, sm::kFireRenderSingleObject, rend, pass, source, lights, 0);
  Virtual(ctx, base, scene, sm::kUseLightsGpuProgram, pass, lights);
  Virtual(ctx, base, render_system, rs::kSetPassIterationCount,
          abi::ReadU32(base, pass + rq::pass::kPassIterationCount.offset));
  ctx.f1.f64 = zero;
  ctx.f2.f64 = zero;
  ctx.f3.f64 = zero;
  Virtual(ctx, base, render_system, rs::kSetDeriveDepthBias, 0);
  Virtual(ctx, base, scene, sm::kUpdateGpuProgramParameters, pass);
  if (Virtual(ctx, base, rend, rd::kPreRender, scene, render_system) & 0xFF)
    Virtual(ctx, base, render_system, rs::kRender, op);
  Virtual(ctx, base, rend, rd::kPostRender, scene, render_system);
  Virtual(ctx, base, scene, sm::kResetViewProjMode, 0);
  ctx.r1.u64 = caller_sp;
}

void InstallSummary();

// The native path when the object qualifies, else the guest's function.
void NativeOrGuest(PPCContext& ctx, uint8_t* base) {
  InstallSummary();
  const Reason reason = Check(base, ctx);
  g_reasons[reason].fetch_add(1, std::memory_order_relaxed);
  if (reason == kNative) {
    Native(ctx, base);
  } else {
    __imp__sub_821C5FA0(ctx, base);
  }
}

void LogReasons() {
  uint64_t counts[kReasonCount];
  uint64_t total = 0;
  for (uint32_t i = 0; i < kReasonCount; ++i) {
    counts[i] = g_reasons[i].exchange(0, std::memory_order_relaxed);
    total += counts[i];
  }
  if (!total) return;
  std::string line;
  for (uint32_t i = 0; i < kReasonCount; ++i) {
    if (counts[i]) line += " " + std::string(kReasonNames[i]) + " " + std::to_string(counts[i]);
  }
  std::string types;
  for (uint32_t t = 0; t < 128; ++t) {
    if (const uint64_t n = g_auto_guest_types[t].exchange(0, std::memory_order_relaxed))
      types += " " + std::to_string(t) + ":" + std::to_string(n);
  }
  REXLOG_INFO("native render object: {} objects:{}; automatic constants: {} on the host, {} the "
              "guest's{}{}",
              total, line, g_auto_native.exchange(0, std::memory_order_relaxed),
              g_auto_guest.exchange(0, std::memory_order_relaxed),
              types.empty() ? "" : " (by type", types.empty() ? "" : types + ")");
}

// The census's summary hook, added on the first call with the native path on (the hooks' slots
// are few).
void InstallSummary() {
  static const bool installed = [] {
    if (!torchlight::live::AddSummaryHook(&LogReasons))
      REXLOG_WARN("native render object: no room for its frame summary hook");
    return true;
  }();
  (void)installed;
}

}  // namespace

extern "C" {

REX_FUNC(sub_821C5FA0) {
  static_assert(rso::kRenderSingleObject == 0x821C5FA0u);
#if TORCHLIGHT_DIAGNOSTICS
  namespace st = torchlight::diag::store_trace;
  if (st::t_reference_run) return __imp__sub_821C5FA0(ctx, base);  // inside a reference run
  if (st::RenderSingleObject(ctx, base, Enabled() ? &NativeOrGuest : &__imp__sub_821C5FA0,
                             &__imp__sub_821C5FA0)) {
    return;
  }
#endif
  if (Enabled()) {
    NativeOrGuest(ctx, base);
  } else {
    __imp__sub_821C5FA0(ctx, base);
  }
}

}  // extern "C"

// Stage 1 P1: GpuProgramParameters::_updateAutoParams (guest_abi render_single_object auto_params)
// on the host for the automatic constants it can transcribe exactly: the matrices, from the guest's
// own AutoParamDataSource getters (same arithmetic, same lazy caches), copied (and transposed) on
// the host: data movement only. A call with any other active entry runs the guest's function, so
// nothing is written before it is known the whole call is transcribed.
namespace {

namespace ap = rso::auto_params;
namespace gpp = torchlight::guest_abi::ogre::gpu_program_parameters;
namespace ace = torchlight::guest_abi::ogre::auto_constant_entry;

const ap::MatrixCase* MatrixCase(uint32_t type) {
  for (const ap::MatrixCase& c : ap::kMatrixCases)
    if (c.type == type) return &c;
  return nullptr;
}

const ap::VectorCase* VectorCase(uint32_t type) {
  for (const ap::VectorCase& c : ap::kByValueVectorCases)
    if (c.type == type) return &c;
  return nullptr;
}

// _writeRawConstant(Matrix4) (0x821C2868).
void WriteMatrix(uint8_t* base, uint32_t params, uint32_t physical, uint32_t matrix,
                 uint32_t count) {
  uint32_t words[ap::kMatrixFloats];
  std::memcpy(words, abi::xbox_memory::HostAddress(base, matrix), sizeof words);
  // Words, not floats: the guest's transpose (0x824639B8) goes through float registers, which
  // would quiet a signalling NaN; no matrix the game builds holds one.
  if (abi::ReadU8(base, params + gpp::kTransposeMatrices.offset)) {
    uint32_t transposed[ap::kMatrixFloats];
    for (uint32_t r = 0; r < 4; ++r)
      for (uint32_t c = 0; c < 4; ++c) transposed[r * 4 + c] = words[c * 4 + r];
    std::memcpy(words, transposed, sizeof words);
  }
  const uint32_t n = count < ap::kMatrixFloats ? count : ap::kMatrixFloats;
  const uint32_t floats = abi::ReadU32(base, params + gpp::kFloatConstants.offset);
  abi::WriteBytes(base, floats + physical * 4, words, n * 4);
}

bool NativeAutoParams(PPCContext& ctx, uint8_t* base) {
  const uint32_t params = ctx.r3.u32, source = ctx.r4.u32, mask = ctx.r5.u32 & 0xFFFF;
  const uint32_t autos = params + gpp::kAutoConstants.offset;
  const uint32_t stride = ace::kSize.bytes;
  if (abi::ReadU32(base, autos) == abi::ReadU32(base, autos + 4)) return true;
  if (!(abi::ReadU16(base, params + gpp::kCombinedVariability.offset) & mask)) return true;
  for (uint32_t e = abi::ReadU32(base, autos), end = abi::ReadU32(base, autos + 4); e != end;
       e += stride) {
    if (!(abi::ReadU16(base, e + ace::kVariability.offset) & mask)) continue;
    const uint32_t type = abi::ReadU32(base, e + ace::kParamType.offset);
    if (type > ap::kMaxType) continue;
    if (type != ap::kWorldMatrixArray3x4 && !MatrixCase(type) && !VectorCase(type)) {
      g_auto_guest.fetch_add(1, std::memory_order_relaxed);
      g_auto_guest_types[type].fetch_add(1, std::memory_order_relaxed);
      return false;
    }
  }
  g_auto_native.fetch_add(1, std::memory_order_relaxed);
  // The guest's frame, for the getters that return by value into it.
  const uint32_t caller_sp = ctx.r1.u32;
  const uint32_t frame = caller_sp - ap::kFrameSize;
  abi::WriteU32(base, frame, caller_sp);
  ctx.r1.u64 = frame;
  abi::WriteU32(base, params + gpp::kActivePassIterationIndex.offset, 0xFFFFFFFFu);
  for (uint32_t e = abi::ReadU32(base, autos); e != abi::ReadU32(base, autos + 4); e += stride) {
    if (!(abi::ReadU16(base, e + ace::kVariability.offset) & mask)) continue;
    const uint32_t type = abi::ReadU32(base, e + ace::kParamType.offset);
    if (type > ap::kMaxType) continue;
    const uint32_t physical = abi::ReadU32(base, e + ace::kPhysicalIndex.offset);
    if (type == ap::kWorldMatrixArray3x4) {
      uint32_t from = Virtual(ctx, base, source, abi::VtableSlot{ap::kArrayGetter / 4,
                                                                 abi::Confidence::kConfirmed});
      uint32_t left = Virtual(ctx, base, source, abi::VtableSlot{ap::kArrayCountGetter / 4,
                                                                 abi::Confidence::kConfirmed});
      uint32_t to = abi::ReadU32(base, params + gpp::kFloatConstants.offset) + physical * 4;
      for (; left; --left, from += ap::kArraySourceStride, to += ap::kArrayMatrixBytes) {
        abi::WriteBytes(base, to, abi::xbox_memory::HostAddress(base, from),
                        ap::kArrayMatrixBytes);
      }
      continue;
    }
    if (const ap::VectorCase* v = VectorCase(type)) {
      // getter(buffer r3, source r4), returning the buffer; then _writeRawConstant(Vector4).
      ctx.r3.u64 = frame + ap::kByValueBuffer;
      ctx.r4.u64 = source;
      Resolve(abi::ReadVirtual(base, source,
                               abi::VtableSlot{v->getter / 4, abi::Confidence::kConfirmed}))(ctx,
                                                                                         base);
      const uint32_t count = abi::ReadU32(base, e + ace::kElementCount.offset);
      const uint32_t n = count < ap::kVectorFloats ? count : ap::kVectorFloats;
      const uint32_t floats = abi::ReadU32(base, params + gpp::kFloatConstants.offset);
      abi::WriteBytes(base, floats + physical * 4, abi::xbox_memory::HostAddress(base, ctx.r3.u32),
                      n * 4);
      continue;
    }
    const ap::MatrixCase* c = MatrixCase(type);
    const abi::VtableSlot getter{c->getter / 4, abi::Confidence::kConfirmed};
    const uint32_t count = abi::ReadU32(base, e + ace::kElementCount.offset);
    const uint32_t matrix =
        c->indexed ? Virtual(ctx, base, source, getter, abi::ReadU32(base, e + ace::kData.offset))
                   : Virtual(ctx, base, source, getter);
    WriteMatrix(base, params, physical, matrix, count);
  }
  ctx.r1.u64 = caller_sp;
  return true;
}

}  // namespace

extern "C" {
REX_EXTERN(__imp__sub_82201EB0);
REX_FUNC(sub_82201EB0) {
  static_assert(rso::auto_params::kUpdateAutoParams == 0x82201EB0u);
#if TORCHLIGHT_DIAGNOSTICS
  // The comparison's reference run is the guest's code all the way down.
  if (torchlight::diag::store_trace::t_reference_run) return __imp__sub_82201EB0(ctx, base);
#endif
  if (!Enabled() || !NativeAutoParams(ctx, base)) __imp__sub_82201EB0(ctx, base);
}
}  // extern "C"

// The XDK vector copy the constant writers use (guest_abi functions::kVectorCopy), as a host
// memcpy where that is the same copy (hooks/guest_copy.h), behind its own switch: it is global (every
// caller, every thread), so the per-call comparison of renderSingleObject checks it only inside
// renderSingleObject; --diag_vector_copy_check checks every copy.
namespace {
// Read once, at the first copy: the step word is the guest's static data.
bool VectorCopyEnabled(const uint8_t* base) {
  static const bool enabled = [base] {
    const uint32_t step = abi::ReadU32(base, torchlight::guest_abi::functions::kVectorCopyStep);
    const bool on = REXCVAR_GET(native_vector_copy) && step == 1;
    REXLOG_INFO("vector copy: {} (--native_vector_copy={}, step word {})",
                on ? "host memcpy where it is the same copy" : "the guest's",
                REXCVAR_GET(native_vector_copy), step);
    return on;
  }();
  return enabled;
}

#if TORCHLIGHT_DIAGNOSTICS
// --diag_vector_copy_check: the guest's copy, then the host's over the restored destination, both
// compared over the whole 128-byte lines around it (a dcbzl outside the destination would show).
// On the render thread only: another thread's write to those lines between the two copies would be
// undone by the restore.
bool CheckedCopy(PPCContext& ctx, uint8_t* base, uint32_t dst, uint32_t src, uint32_t size) {
  static std::atomic<uint64_t> checked{0}, differing{0}, unchecked{0};
  static const std::thread::id render_thread = std::this_thread::get_id();
  if (std::this_thread::get_id() != render_thread) return false;
  if (size > (1u << 20)) {
    if (unchecked.fetch_add(1, std::memory_order_relaxed) == 0)
      REXLOG_INFO("vector copy check: copies over 1 MiB are not checked");
    return false;
  }
  const uint32_t first = dst & ~127u;
  const uint32_t span = ((dst + size + 127u) & ~127u) - first;
  uint8_t* lines = abi::xbox_memory::HostAddress(base, first);
  std::vector<uint8_t> before(lines, lines + span);
  __imp__sub_821C28D8(ctx, base);
  std::vector<uint8_t> guest(lines, lines + span);
  std::memcpy(lines, before.data(), span);
  torchlight::hooks::HostGuestCopy(base, dst, src, size);
  const uint64_t n = checked.fetch_add(1, std::memory_order_relaxed) + 1;
  if (std::memcmp(lines, guest.data(), span) != 0) {
    if (differing.fetch_add(1, std::memory_order_relaxed) < 20)
      REXLOG_WARN("vector copy check: {:08X} <- {:08X} ({} bytes) differs from the guest's copy",
                  dst, src, size);
    std::memcpy(lines, guest.data(), span);
  }
  if (n % 1000000 == 0)
    REXLOG_INFO("vector copy check: {} copies compared, {} differ, {} over 1 MiB not checked", n,
                differing.load(std::memory_order_relaxed),
                unchecked.load(std::memory_order_relaxed));
  ctx.r3.u64 = dst;
  return true;
}
#endif
}  // namespace

extern "C" {
static_assert(torchlight::guest_abi::functions::kVectorCopy.address == 0x821C28D8u);
REX_FUNC(sub_821C28D8) {
  const uint32_t dst = ctx.r3.u32, src = ctx.r4.u32, size = ctx.r5.u32;
#if TORCHLIGHT_DIAGNOSTICS
  if (torchlight::diag::store_trace::t_reference_run) return __imp__sub_821C28D8(ctx, base);
#endif
  if (!VectorCopyEnabled(base)) return __imp__sub_821C28D8(ctx, base);
#if TORCHLIGHT_DIAGNOSTICS
  if (REXCVAR_GET(diag_vector_copy_check) && size &&
      torchlight::hooks::RouteGuestCopy(dst, src, size) == torchlight::hooks::GuestCopyRoute::kHost &&
      CheckedCopy(ctx, base, dst, src, size)) {
    return;
  }
#endif
  if (!torchlight::hooks::HostGuestCopy(base, dst, src, size)) __imp__sub_821C28D8(ctx, base);
}
}  // extern "C"
