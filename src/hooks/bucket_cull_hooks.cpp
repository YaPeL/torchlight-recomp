// Bucket culling hooks (bucket_cull.h; --native_bucket_cull, native mode only, on by default).
//
// Runic's _findVisibleObjects marks the walk of the scene's main viewport; during it, every
// StaticGeometry::GeometryBucket that reaches RenderQueue::addRenderable is tested with its own
// box against the camera's frustum (the planes the guest's own node test reads) and, when entirely
// outside, not queued. The other walks (the light map and shadow cameras, other scene managers)
// and every other renderable are left alone. Bucket boxes are computed on first use from all the
// vertices of the bucket's vertex data and forgotten in ~GeometryBucket.

#include <cstdint>
#include <cstring>
#include <string_view>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include "capture/guest_readers.h"
#include "commands/types.h"
#include "guest_abi/ogre_layout.h"
#include "guest_abi/static_geometry.h"
#include "hooks/bucket_cull.h"
#include "hooks/bucket_cull_hooks.h"
#include "hooks/video_mode_hooks.h"

REXCVAR_DEFINE_BOOL(native_bucket_cull, true, "Torchlight",
                    "Native mode only: drop the StaticGeometry buckets entirely outside the main "
                    "camera's frustum (an improvement over the original; hooks/bucket_cull.h); "
                    "false queues every bucket as the game does");

namespace {

namespace abi = torchlight::guest_abi;
namespace sg = torchlight::guest_abi::static_geometry;
namespace cap = torchlight::capture;
using namespace torchlight::hooks;

constexpr uint64_t kReportWalks = 600;  // the log line's period, in main walks

bool g_enabled = false;           // InstallBucketCull: native mode and the cvar
bool g_programs_ok = true;        // every vertex program seen places vertices as the fixed pipeline
bool g_main_walk = false;         // inside _findVisibleObjects for the scene viewport
Planes g_planes{};                // that walk's frustum
BucketBoxes g_boxes;
BucketCullStats g_stats;

Planes ReadPlanes(const uint8_t* m, uint32_t camera) {
  const uint32_t cull = abi::ReadU32(m, camera + sg::camera::kCullFrustum.offset);
  const uint32_t frustum = cull ? cull : camera;
  Planes p;
  for (uint32_t i = 0; i < sg::frustum::kPlaneCount; ++i)
    for (uint32_t k = 0; k < 4; ++k)
      p[i][k] = abi::ReadF32(m, frustum + sg::frustum::kPlanes.offset + 16 * i + 4 * k);
  return p;
}

// The bucket's box in its region's space: every vertex of its vertex data (vertexStart through
// vertexCount, whatever the indices use), position element as FLOAT3. Nothing when the data is not
// built or not readable; such a bucket is always queued.
std::optional<Box> ComputeBucketBox(const uint8_t* m, uint32_t bucket) {
  namespace og = abi::ogre;
  const uint32_t vdata = abi::ReadU32(m, bucket + sg::geometry_bucket::kVertexData.offset);
  if (!vdata) return std::nullopt;
  const uint32_t start = abi::ReadU32(m, vdata + og::vertex_data::kVertexStart.offset);
  const uint32_t count = abi::ReadU32(m, vdata + og::vertex_data::kVertexCount.offset);
  if (count == 0) return std::nullopt;
  for (const auto& e : cap::ReadVertexElements(
           m, abi::ReadU32(m, vdata + og::vertex_data::kVertexDeclaration.offset))) {
    if (!e.semantic.known() || e.semantic.get() != torchlight::commands::VertexSemantic::kPosition)
      continue;
    if (!e.type.known() || e.type.get() != torchlight::commands::VertexType::kFloat3)
      return std::nullopt;
    const uint32_t buffer = cap::BoundVertexBuffer(
        m, abi::ReadU32(m, vdata + og::vertex_data::kVertexBufferBinding.offset), e.source);
    if (!buffer) return std::nullopt;
    const auto memory = cap::VertexBufferMemory(m, buffer);
    const uint32_t stride = abi::ReadU32(m, buffer + og::hardware_vertex_buffer::kVertexSize.offset);
    if (!memory || stride < 12 || e.offset + 12 > stride) return std::nullopt;
    if (uint64_t(start + count) * stride > memory->size) return std::nullopt;
    BoxBuilder b;
    for (uint32_t v = start; v < start + count; ++v) {
      const uint8_t* p = m + memory->address + v * stride + e.offset;
      b.Add(FetchFloat(p, memory->fetch_endian), FetchFloat(p + 4, memory->fetch_endian),
            FetchFloat(p + 8, memory->fetch_endian));
    }
    return b.Result();
  }
  return std::nullopt;
}

// Whether the main walk should drop this renderable: a geometry bucket whose box, under its region
// node's current transform, lies entirely outside the frustum.
bool OutsideMainCamera(const uint8_t* m, uint32_t renderable) {
  if (abi::ReadU32(m, renderable) != sg::geometry_bucket::kVtable) return false;
  ++g_stats.buckets_tested;
  const Box* local = g_boxes.Find(renderable);
  if (!local) {
    const auto box = ComputeBucketBox(m, renderable);
    if (!box) {
      ++g_stats.buckets_unreadable;
      return false;
    }
    g_boxes.Store(renderable, *box);
    local = g_boxes.Find(renderable);
  }
  const uint32_t material = abi::ReadU32(m, renderable + sg::geometry_bucket::kParent.offset);
  const uint32_t lod = material ? abi::ReadU32(m, material + sg::material_bucket::kParent.offset) : 0;
  const uint32_t region = lod ? abi::ReadU32(m, lod + sg::lod_bucket::kParent.offset) : 0;
  const uint32_t node = region ? abi::ReadU32(m, region + sg::movable_object::kParentNode.offset) : 0;
  if (!node) {
    ++g_stats.buckets_no_node;
    return false;
  }
  if (abi::ReadU8(m, node + sg::node::kCachedTransformOutOfDate.offset)) {
    ++g_stats.buckets_stale_transform;
    return false;
  }
  Matrix world;
  for (uint32_t i = 0; i < 16; ++i)
    world[i] = abi::ReadF32(m, node + sg::node::kCachedTransform.offset + 4 * i);
  return !BoxVisible(g_planes, TransformBox(*local, world));
}

}  // namespace

namespace torchlight::hooks {

void InstallBucketCull(bool native_only) {
  g_enabled = native_only && REXCVAR_GET(native_bucket_cull);
  REXLOG_INFO("bucket culling: {} (--native_bucket_cull={}, native mode {})",
              g_enabled ? "on" : "off", REXCVAR_GET(native_bucket_cull),
              native_only ? "on" : "off");
}

void BucketCullNoteProgram(std::string_view target, std::string_view source) {
  if (!g_programs_ok || target.substr(0, 2) != "vs" || PlacesVertexLikeFixedPipeline(source))
    return;
  g_programs_ok = false;
  REXLOG_WARN("bucket culling: off from now on, a vertex program places vertices its own way");
}

bool BucketCulledInMainWalk(const uint8_t* base, uint32_t renderable) {
  return g_main_walk && g_programs_ok && OutsideMainCamera(base, renderable);
}

}  // namespace torchlight::hooks

extern "C" {

REX_EXTERN(__imp__sub_821A6070);
REX_FUNC(sub_821A6070) {
  static_assert(sg::kFindVisibleObjects == 0x821A6070u);
  const uint32_t sm = ctx.r3.u32, camera = ctx.r4.u32;
  const uint32_t viewport = abi::ReadU32(base, sm + sg::scene_manager::kCurrentViewport.offset);
  g_main_walk = viewport && viewport == SceneViewport();
  if (g_main_walk) {
    ++g_stats.main_walks;
    g_planes = ReadPlanes(base, camera);
  }
  __imp__sub_821A6070(ctx, base);
  if (g_main_walk && g_enabled && g_stats.main_walks == kReportWalks) {
    const double w = double(g_stats.main_walks);
    REXLOG_INFO("bucket culling: last {} main walks; per walk: {:.1f} buckets tested, {:.1f} "
                "dropped, {:.1f} unreadable, {:.1f} without a node, {:.1f} with a stale node "
                "transform; {} boxes cached{}",
                g_stats.main_walks, g_stats.buckets_tested / w, g_stats.buckets_dropped / w,
                g_stats.buckets_unreadable / w, g_stats.buckets_no_node / w,
                g_stats.buckets_stale_transform / w, g_boxes.size(),
                g_programs_ok ? "" : " (off: a vertex program places vertices its own way)");
    g_stats = BucketCullStats{};
  }
  g_main_walk = false;
}

REX_EXTERN(__imp__sub_821C3730);
REX_FUNC(sub_821C3730) {
  static_assert(sg::kAddRenderable == 0x821C3730u);
  if (g_enabled && BucketCulledInMainWalk(base, ctx.r4.u32)) {
    ++g_stats.buckets_dropped;
    return;  // RenderQueue::addRenderable returns nothing
  }
  __imp__sub_821C3730(ctx, base);
}

REX_EXTERN(__imp__sub_8247F728);
REX_FUNC(sub_8247F728) {
  static_assert(sg::geometry_bucket::kDestructor == 0x8247F728u);
  g_boxes.Forget(ctx.r3.u32);
  __imp__sub_8247F728(ctx, base);
}

}  // extern "C"
