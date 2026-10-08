#include "hooks/bucket_cull.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

namespace torchlight::hooks {

bool BoxVisible(const Planes& planes, const Box& box) {
  for (const auto& p : planes) {
    float dist = p[3], max_abs = 0;
    for (int i = 0; i < 3; ++i) {
      const float centre = (box.min[i] + box.max[i]) * 0.5f;
      const float half = (box.max[i] - box.min[i]) * 0.5f;
      dist += p[i] * centre;
      max_abs += std::fabs(p[i] * half);
    }
    if (dist < -max_abs) return false;
  }
  return true;
}

void BoxBuilder::Add(float x, float y, float z) {
  const float v[3] = {x, y, z};
  for (int i = 0; i < 3; ++i) {
    box_.min[i] = empty_ ? v[i] : std::min(box_.min[i], v[i]);
    box_.max[i] = empty_ ? v[i] : std::max(box_.max[i], v[i]);
  }
  empty_ = false;
}

std::optional<Box> BoxBuilder::Result() const {
  if (empty_) return std::nullopt;
  for (int i = 0; i < 3; ++i)
    if (!std::isfinite(box_.min[i]) || !std::isfinite(box_.max[i])) return std::nullopt;
  return box_;
}

Box TransformBox(const Box& box, const Matrix& m) {
  BoxBuilder b;
  for (int corner = 0; corner < 8; ++corner) {
    const float p[3] = {corner & 1 ? box.max[0] : box.min[0], corner & 2 ? box.max[1] : box.min[1],
                        corner & 4 ? box.max[2] : box.min[2]};
    float w[3];
    for (int r = 0; r < 3; ++r) w[r] = m[r * 4] * p[0] + m[r * 4 + 1] * p[1] + m[r * 4 + 2] * p[2] + m[r * 4 + 3];
    b.Add(w[0], w[1], w[2]);
  }
  return *b.Result();
}

float FetchFloat(const uint8_t* stored, uint32_t mode) {
  // The swap the backend applies to a whole vertex buffer (SwapFetchEndian), on one 32-bit word;
  // the result is in host (little-endian) order.
  uint8_t d[4] = {stored[0], stored[1], stored[2], stored[3]};
  switch (mode) {
    case 1:  // 8 in 16
      std::swap(d[0], d[1]);
      std::swap(d[2], d[3]);
      break;
    case 2:  // 8 in 32
      std::swap(d[0], d[3]);
      std::swap(d[1], d[2]);
      break;
    case 3:  // 16 in 32
      std::swap(d[0], d[2]);
      std::swap(d[1], d[3]);
      break;
    default:
      break;
  }
  uint32_t u = uint32_t(d[0]) | uint32_t(d[1]) << 8 | uint32_t(d[2]) << 16 | uint32_t(d[3]) << 24;
  return std::bit_cast<float>(u);
}

bool PlacesVertexLikeFixedPipeline(std::string_view source) {
  // The RTSS's output position lines (every vertex program of the 41 in our captures has one of
  // the two): FFP_Transform.cpp's fixed transform and the hardware skinning sub-render state's.
  constexpr std::string_view kFixed = "FFP_Transform(worldviewproj_matrix, iPos_0, oPos_0);";
  constexpr std::string_view kSkinned = "FFP_Transform(viewproj_matrix, lLocalParam_0, oPos_0);";
  if (source.find(kSkinned) != std::string_view::npos) return true;
  if (source.find(kFixed) == std::string_view::npos) return false;
  // Nothing else writes the output position (it appears in its declaration and in that line
  // only) and the input position is never an output (RTSS functions take their outputs last).
  size_t uses = 0;
  for (size_t at = source.find("oPos_0"); at != std::string_view::npos;
       at = source.find("oPos_0", at + 1))
    ++uses;
  return uses == 2 && source.find("iPos_0)") == std::string_view::npos;
}

namespace {

Box Merge(const Box& a, const Box& b) {
  Box m;
  for (int i = 0; i < 3; ++i) {
    m.min[i] = std::min(a.min[i], b.min[i]);
    m.max[i] = std::max(a.max[i], b.max[i]);
  }
  return m;
}

float Extent(const Box& b) {
  float e = 0;
  for (int i = 0; i < 3; ++i) e = std::max(e, b.max[i] - b.min[i]);
  return e;
}

}  // namespace

std::optional<BucketShape> BuildShape(std::span<const std::array<float, 3>> positions,
                                      std::span<const uint32_t> indices, bool pieces,
                                      size_t max_pieces) {
  if (indices.size() < 3) return std::nullopt;
  for (uint32_t i : indices)
    if (i >= positions.size()) return std::nullopt;
  // Union-find over the vertices the triangles use.
  std::vector<uint32_t> parent(positions.size());
  for (uint32_t i = 0; i < parent.size(); ++i) parent[i] = i;
  auto find = [&](uint32_t x) {
    while (parent[x] != x) x = parent[x] = parent[parent[x]];
    return x;
  };
  const size_t triangles = indices.size() / 3;
  for (size_t t = 0; t < triangles; ++t) {
    const uint32_t a = find(indices[t * 3]), b = find(indices[t * 3 + 1]), c = find(indices[t * 3 + 2]);
    parent[a] = b;
    parent[find(c)] = find(b);
  }
  std::unordered_map<uint32_t, BoxBuilder> components;
  BoxBuilder whole;
  for (size_t k = 0; k < triangles * 3; ++k) {
    const auto& p = positions[indices[k]];
    components[find(indices[k])].Add(p[0], p[1], p[2]);
    whole.Add(p[0], p[1], p[2]);
  }
  auto whole_box = whole.Result();
  if (!whole_box) return std::nullopt;
  BucketShape shape{*whole_box, {}};
  if (!pieces || components.size() < 2) return shape;
  std::vector<Box> boxes;
  for (auto& [root, builder] : components) boxes.push_back(*builder.Result());
  // Group into at most max_pieces: split the group of largest extent at the median of its boxes'
  // centres along its longest axis, until there are max_pieces groups.
  std::vector<std::vector<Box>> groups{std::move(boxes)};
  while (groups.size() < max_pieces) {
    size_t widest = groups.size();
    float widest_extent = -1;
    for (size_t g = 0; g < groups.size(); ++g) {
      if (groups[g].size() < 2) continue;
      Box all = groups[g][0];
      for (const Box& b : groups[g]) all = Merge(all, b);
      if (Extent(all) > widest_extent) { widest_extent = Extent(all); widest = g; }
    }
    if (widest == groups.size()) break;  // every group is a single piece
    auto& g = groups[widest];
    Box all = g[0];
    for (const Box& b : g) all = Merge(all, b);
    int axis = 0;
    for (int i = 1; i < 3; ++i)
      if (all.max[i] - all.min[i] > all.max[axis] - all.min[axis]) axis = i;
    std::sort(g.begin(), g.end(), [axis](const Box& a, const Box& b) {
      return a.min[axis] + a.max[axis] < b.min[axis] + b.max[axis];
    });
    std::vector<Box> upper(g.begin() + g.size() / 2, g.end());
    g.resize(g.size() / 2);
    groups.push_back(std::move(upper));
  }
  for (const auto& g : groups) {
    Box all = g[0];
    for (const Box& b : g) all = Merge(all, b);
    shape.pieces.push_back(all);
  }
  return shape;
}

bool ShapeVisible(const Planes& planes, const BucketShape& shape, const Matrix& world) {
  if (!BoxVisible(planes, TransformBox(shape.whole, world))) return false;
  if (shape.pieces.empty()) return true;
  for (const Box& piece : shape.pieces)
    if (BoxVisible(planes, TransformBox(piece, world))) return true;
  return false;
}

const BucketShape* BucketBoxes::Find(uint32_t bucket) const {
  auto it = boxes_.find(bucket);
  return it == boxes_.end() ? nullptr : &it->second;
}

}  // namespace torchlight::hooks
