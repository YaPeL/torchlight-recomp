// Bucket culling hooks (bucket_cull.h; --native_bucket_cull, native mode only, on by default).
//
// Runic's _findVisibleObjects marks the walk of the scene's main viewport; during it, every
// StaticGeometry::GeometryBucket that reaches RenderQueue::addRenderable is tested with its own
// box against the camera's frustum (the planes the guest's own node test reads) and, when entirely
// outside, not queued. The other walks (the light map and shadow cameras, other scene managers)
// and every other renderable are left alone. Bucket boxes are computed on first use from all the
// vertices of the bucket's vertex data and forgotten in ~GeometryBucket.

#include <array>
#include <chrono>
#include <cstdint>
#include <vector>
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
REXCVAR_DEFINE_BOOL(native_bucket_cull_pieces, true, "Torchlight",
                    "With --native_bucket_cull: test each piece of a bucket (up to 16 boxes) "
                    "instead of one box for the whole bucket");

namespace {

namespace abi = torchlight::guest_abi;
namespace sg = torchlight::guest_abi::static_geometry;
namespace cap = torchlight::capture;
using namespace torchlight::hooks;

constexpr uint64_t kReportWalks = 600;  // the log line's period, in main walks

bool g_enabled = false;           // InstallBucketCull: native mode and the cvar
bool g_pieces = true;             // --native_bucket_cull_pieces: one box per piece
bool g_programs_ok = true;        // every vertex program seen places vertices as the fixed pipeline
bool g_main_walk = false;         // inside _findVisibleObjects for the scene viewport
Planes g_planes{};                // that walk's frustum, read at its first bucket
uint32_t g_walk_camera = 0;       // that walk's camera
bool g_planes_read = false;       // g_planes holds this walk's planes
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

// The bucket's shape in its region's space. The whole box covers every vertex of its vertex data
// (vertexStart through vertexCount, whatever the indices use); the pieces come from the triangles
// of its index data (a triangle list, indices relative to vertexStart). Positions are the FLOAT3
// position element. Nothing when the data is not built or not readable; such a bucket is always
// queued.
std::optional<BucketShape> ComputeBucketShape(const uint8_t* m, uint32_t bucket, bool pieces) {
  namespace og = abi::ogre;
  const uint32_t vdata = abi::ReadU32(m, bucket + sg::geometry_bucket::kVertexData.offset);
  const uint32_t idata = abi::ReadU32(m, bucket + sg::geometry_bucket::kIndexData.offset);
  if (!vdata || !idata) return std::nullopt;
  const uint32_t start = abi::ReadU32(m, vdata + og::vertex_data::kVertexStart.offset);
  const uint32_t count = abi::ReadU32(m, vdata + og::vertex_data::kVertexCount.offset);
  if (count == 0) return std::nullopt;
  std::vector<std::array<float, 3>> positions;
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
    positions.resize(count);
    for (uint32_t v = 0; v < count; ++v) {
      const uint8_t* p = memory->bytes + (start + v) * stride + e.offset;
      positions[v] = {FetchFloat(p, memory->fetch_endian), FetchFloat(p + 4, memory->fetch_endian),
                      FetchFloat(p + 8, memory->fetch_endian)};
    }
    break;
  }
  if (positions.empty()) return std::nullopt;
  BoxBuilder all;
  for (const auto& p : positions) all.Add(p[0], p[1], p[2]);
  const auto all_box = all.Result();
  if (!all_box) return std::nullopt;
  // The triangles (big-endian indices in guest memory).
  const uint32_t ibuffer = abi::ReadU32(m, idata + og::index_data::kIndexBuffer.offset +
                                               og::shared_ptr::kPRep.offset);
  const uint32_t istart = abi::ReadU32(m, idata + og::index_data::kIndexStart.offset);
  const uint32_t icount = abi::ReadU32(m, idata + og::index_data::kIndexCount.offset);
  const uint32_t isize = ibuffer ? abi::ReadU32(m, ibuffer + og::hardware_index_buffer::kIndexSize.offset) : 0;
  const auto imemory = ibuffer ? cap::IndexBufferMemory(m, ibuffer) : std::nullopt;
  std::optional<BucketShape> shape;
  if (imemory && (isize == 2 || isize == 4) && icount >= 3 && icount % 3 == 0 &&
      uint64_t(istart + icount) * isize <= imemory->size) {
    std::vector<uint32_t> indices(icount);
    for (uint32_t k = 0; k < icount; ++k) {
      const uint8_t* q = imemory->bytes + uint64_t(istart + k) * isize;
      indices[k] = isize == 4 ? uint32_t(q[0]) << 24 | uint32_t(q[1]) << 16 | uint32_t(q[2]) << 8 | q[3]
                              : uint32_t(q[0]) << 8 | q[1];
    }
    shape = BuildShape(positions, indices, pieces);
  }
  if (!shape) return BucketShape{*all_box, {}};  // no usable triangles: the whole box only
  // The whole box keeps every vertex (a larger box is safe).
  shape->whole = *all_box;
  return shape;
}

// Whether the main walk should drop this renderable: a geometry bucket whose box, under its region
// node's current transform, lies entirely outside the frustum.
bool OutsideMainCamera(const uint8_t* m, uint32_t renderable) {
  if (abi::ReadU32(m, renderable) != sg::geometry_bucket::kVtable) return false;
  ++g_stats.buckets_tested;
  // The walk brings the camera's planes up to date first (0x821A6070 asks the camera for a
  // frustum plane, slot 91, @0x821A60C8, before walking); read them when the first bucket arrives,
  // not before the walk, when they are still the previous frame's.
  if (!g_planes_read) {
    g_planes = ReadPlanes(m, g_walk_camera);
    g_planes_read = true;
  }
  const BucketShape* local = g_boxes.Find(renderable);
  if (!local) {
    const auto started = std::chrono::steady_clock::now();
    auto shape = ComputeBucketShape(m, renderable, g_pieces);
    g_stats.shape_ms +=
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    if (!shape) {
      ++g_stats.buckets_unreadable;
      return false;
    }
    ++g_stats.shapes_built;
    g_stats.pieces_built += shape->pieces.size();
    g_boxes.Store(renderable, std::move(*shape));
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
  return !ShapeVisible(g_planes, *local, world);
}

}  // namespace

namespace torchlight::hooks {

void InstallBucketCull(bool native_only) {
  g_enabled = native_only && REXCVAR_GET(native_bucket_cull);
  g_pieces = REXCVAR_GET(native_bucket_cull_pieces);
  REXLOG_INFO("bucket culling: {}{} (--native_bucket_cull={}, --native_bucket_cull_pieces={}, "
              "native mode {})",
              g_enabled ? "on" : "off", g_enabled ? (g_pieces ? ", per piece" : ", per bucket") : "",
              REXCVAR_GET(native_bucket_cull), g_pieces, native_only ? "on" : "off");
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
    g_walk_camera = camera;
    g_planes_read = false;
  }
  __imp__sub_821A6070(ctx, base);
  if (g_main_walk && g_enabled && g_stats.main_walks == kReportWalks) {
    const double w = double(g_stats.main_walks);
    REXLOG_INFO("bucket culling: last {} main walks; per walk: {:.1f} buckets tested, {:.1f} "
                "dropped, {:.1f} unreadable, {:.1f} without a node, {:.1f} with a stale node "
                "transform; {} shapes built ({} pieces) in {:.2f} ms; {} cached{}",
                g_stats.main_walks, g_stats.buckets_tested / w, g_stats.buckets_dropped / w,
                g_stats.buckets_unreadable / w, g_stats.buckets_no_node / w,
                g_stats.buckets_stale_transform / w, g_stats.shapes_built, g_stats.pieces_built,
                g_stats.shape_ms, g_boxes.size(),
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
