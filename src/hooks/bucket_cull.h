// Bucket culling (--native_bucket_cull, on by default; false turns it off): an improvement over
// the original game.
//
// The guest culls per scene node (Runic's octree walk, docs/guest-hot-paths.md "Culling"), and a
// StaticGeometry region is one object with one box covering a large piece of the level; every
// geometry bucket of a region whose box touches the frustum is queued, one draw per material and
// vertex format, and in the slow scenes 25-58 % of them lie entirely outside the screen (the PC
// build does the same). This gives each bucket its own box, computed once from all the vertices of
// its vertex data (in the region's space), brought to world space at each test with the region
// node's current transform, and drops the buckets entirely outside the main camera's frustum
// before they reach the render queue. Only the main scene pass is touched; the light map and
// shadow passes (other cameras) queue every bucket as before.
//
// This file is the host-side logic, with no guest access: boxes, planes and the per-bucket cache.
// The hooks (bucket_cull_hooks.cpp) read the guest and decide.

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <span>
#include <unordered_map>
#include <vector>

namespace torchlight::hooks {

struct Box {
  std::array<float, 3> min{}, max{};
};

// Six planes {normal xyz, d}, normals pointing inside (OGRE's Frustum planes).
using Planes = std::array<std::array<float, 4>, 6>;

// Frustum::isVisible(AABB) for a finite box (the guest's node test, 0x821C74A0): false only when
// the box is entirely on the outer side of one plane.
bool BoxVisible(const Planes& planes, const Box& box);

// Row-major affine transform (OGRE Matrix4, m[row * 4 + col], translation in column 3).
using Matrix = std::array<float, 16>;

// The box of a set of positions.
class BoxBuilder {
 public:
  void Add(float x, float y, float z);
  std::optional<Box> Result() const;

 private:
  Box box_;
  bool empty_ = true;
};

// The box of the eight transformed corners of `box` (contains the transformed box).
Box TransformBox(const Box& box, const Matrix& m);

// A vertex fetch float (32 bits as stored in guest memory, `mode` = the fetch swap mode of
// commands::BlobEndian::kVertexFetch) as a host float.
float FetchFloat(const uint8_t* stored, uint32_t mode);

// Whether a vertex program places the vertex exactly as the fixed pipeline does, from what the
// guest's RTSS writes for the output position: the world-view-projection transform of the input
// position, or the hardware skinning transform. Any other program could move geometry outside
// a box computed from the vertex data, so the bucket culling stays off once one appears.
bool PlacesVertexLikeFixedPipeline(std::string_view vertex_program_source);

// The most boxes one bucket keeps. A bucket gathers every piece of geometry of one material in a
// region (in the town, the same building texture on buildings all around the square), so one box
// for all of it touches the frustum even when every piece is off screen; one box per piece catches
// that. The pieces are the connected components of its triangles; beyond this many, the nearest
// are grouped (median splits along the longest axis) so a frame costs at most this many box tests
// per bucket, and the first visible piece ends the test.
inline constexpr size_t kMaxPiecesPerBucket = 16;

// A bucket's boxes in its region's space: the whole bucket, then its pieces (empty when the whole
// box is the only one: one piece, or pieces turned off).
struct BucketShape {
  Box whole;
  std::vector<Box> pieces;
};

// The shape of triangles given by `indices` (a triangle list, three per triangle, into
// `positions`): the whole box and, when `pieces`, the boxes of the connected components, grouped
// to at most `max_pieces`. Nothing when there are no triangles or a position is not finite.
std::optional<BucketShape> BuildShape(std::span<const std::array<float, 3>> positions,
                                      std::span<const uint32_t> indices, bool pieces,
                                      size_t max_pieces = kMaxPiecesPerBucket);

// Whether any of the shape lies inside the frustum under `world`: the whole box first, then (when
// there are pieces) each piece until one is visible.
bool ShapeVisible(const Planes& planes, const BucketShape& shape, const Matrix& world);

// Per-bucket shapes, by guest bucket address; forgotten when the bucket is destroyed.
class BucketBoxes {
 public:
  const BucketShape* Find(uint32_t bucket) const;
  void Store(uint32_t bucket, BucketShape shape) { boxes_[bucket] = std::move(shape); }
  void Forget(uint32_t bucket) { boxes_.erase(bucket); }
  size_t size() const { return boxes_.size(); }

 private:
  std::unordered_map<uint32_t, BucketShape> boxes_;
};

}  // namespace torchlight::hooks
