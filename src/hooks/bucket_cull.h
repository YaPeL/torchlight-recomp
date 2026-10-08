// Bucket culling (--native_bucket_cull, off by default): an improvement over the original game.
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
#include <unordered_map>

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

// Per-bucket boxes in the region's space, by guest bucket address; forgotten when the bucket is
// destroyed.
class BucketBoxes {
 public:
  const Box* Find(uint32_t bucket) const;
  void Store(uint32_t bucket, const Box& box) { boxes_[bucket] = box; }
  void Forget(uint32_t bucket) { boxes_.erase(bucket); }
  size_t size() const { return boxes_.size(); }

 private:
  std::unordered_map<uint32_t, Box> boxes_;
};

}  // namespace torchlight::hooks
