#include "hooks/queue_census.h"

#include <vector>

#include "guest_abi/ogre_layout.h"
#include "guest_abi/render_queue.h"

namespace torchlight::hooks {

namespace abi = torchlight::guest_abi;
namespace rq = abi::render_queue;

const char* QueueReasonName(QueueReason r) {
  switch (r) {
    case QueueReason::kNative: return "qualifying";
    case QueueReason::kNotMainScene: return "projector";
    case QueueReason::kNotSolidsBasic: return "transparent";
    case QueueReason::kNoPrograms: return "no_programs";
    case QueueReason::kGeometryProgram: return "geometry_program";
    case QueueReason::kIteration: return "iterations";
    case QueueReason::kPerLight: return "per_light";
    case QueueReason::kLightScissorClip: return "light_scissor_clip";
    case QueueReason::kDepthBias: return "depth_bias";
    case QueueReason::kRenderableClass: return "renderable_class";
    case QueueReason::kSkinned: return "skinned";
    case QueueReason::kCount: break;
  }
  return "?";
}

void QueueCensusTotals::Add(const QueueCensusTotals& o) {
  collections += o.collections;
  mixed_collections += o.mixed_collections;
  groups += o.groups;
  mixed_groups += o.mixed_groups;
  groups_from_minus_one += o.groups_from_minus_one;
  draws += o.draws;
  draws_native_whole += o.draws_native_whole;
  draws_native_group += o.draws_native_group;
  ms += o.ms;
  ms_native_whole += o.ms_native_whole;
  ms_native_group += o.ms_native_group;
  for (size_t i = 0; i < draws_by_reason.size(); ++i) draws_by_reason[i] += o.draws_by_reason[i];
}

QueueReason ClassifyPass(const uint8_t* m, uint32_t pass) {
  namespace p = rq::pass;
  if (!abi::ReadU32(m, pass, p::kVertexProgramUsage) || !abi::ReadU32(m, pass, p::kFragmentProgramUsage))
    return QueueReason::kNoPrograms;
  if (abi::ReadU32(m, pass, p::kGeometryProgramUsage)) return QueueReason::kGeometryProgram;
  if (abi::ReadU32(m, pass, p::kPassIterationCount) != 1) return QueueReason::kIteration;
  if (abi::ReadBool(m, pass, p::kIteratePerLight)) return QueueReason::kPerLight;
  if (abi::ReadBool(m, pass, p::kLightScissoring) || abi::ReadBool(m, pass, p::kLightClipPlanes))
    return QueueReason::kLightScissorClip;
  if (abi::ReadF32(m, pass, p::kDepthBiasConstant) != 0.0f ||
      abi::ReadF32(m, pass, p::kDepthBiasSlopeScale) != 0.0f ||
      abi::ReadF32(m, pass, p::kDepthBiasPerIteration) != 0.0f)
    return QueueReason::kDepthBias;
  return QueueReason::kNative;
}

QueueReason ClassifyRenderable(const uint8_t* m, uint32_t renderable) {
  const uint32_t vtable = abi::ReadU32(m, renderable);
  if (vtable == rq::renderable_class::kGeometryBucketVtable) return QueueReason::kNative;
  if (vtable != rq::renderable_class::kSubEntityVtable) return QueueReason::kRenderableClass;
  const uint32_t entity = abi::ReadU32(m, renderable, rq::sub_entity::kParentEntity);
  if (!entity || abi::ReadU16(m, entity, rq::entity::kNumBoneMatrices)) return QueueReason::kSkinned;
  return QueueReason::kNative;
}

namespace {

// One unit the per-group fallback decides on: a pass group of the grouped walk, or one entry of a
// sorted walk.
struct Unit {
  uint32_t draws = 0;
  QueueReason first_reason = QueueReason::kNative;  // the reason of its first failing object
  uint32_t failing = 0;                             // objects that fail
  bool from_minus_one = false;
};

void AddObject(Unit& u, QueueReason r) {
  ++u.draws;
  if (r == QueueReason::kNative) return;
  if (!u.failing) u.first_reason = r;
  ++u.failing;
}

// MSVC std::map in order: the head node is also the nil of every leaf (ogre_layout stl_tree).
template <typename F>
void ForEachNode(const uint8_t* m, uint32_t map, F&& f) {
  namespace t = abi::ogre::stl_tree;
  const uint32_t head = abi::ReadU32(m, map, t::kHead);
  if (!head) return;
  // Every loop is bounded, so a map read while it changes cannot hang the guest's thread.
  constexpr int kGuard = 1 << 20;
  int steps = 0;
  uint32_t node = abi::ReadU32(m, head, t::kNodeLeft);  // the leftmost node, or head when empty
  while (node != head && ++steps < kGuard) {
    f(node);
    const uint32_t right = abi::ReadU32(m, node, t::kNodeRight);
    if (right != head) {
      node = right;
      for (uint32_t left; (left = abi::ReadU32(m, node, t::kNodeLeft)) != head && ++steps < kGuard;)
        node = left;
    } else {
      uint32_t parent = abi::ReadU32(m, node, t::kNodeParent);
      while (parent != head && node == abi::ReadU32(m, parent, t::kNodeRight) && ++steps < kGuard) {
        node = parent;
        parent = abi::ReadU32(m, node, t::kNodeParent);
      }
      node = parent;
    }
  }
}

}  // namespace

void ClassifyCollection(const uint8_t* m, uint32_t collection, uint32_t om, bool main_scene,
                        QueueCollection kind, double ms, QueueCensusTotals& totals) {
  namespace qc = rq::queued_renderable_collection;
  // acceptVisitor's choice as the guest compiles it (0x821C08B8): a mode not notified falls back to
  // the grouped walk (bit 1), else to the backward walk for either sort bit (mask 6, @0x821C08E4);
  // then 1, 2 and 6 walk, any other mode walks nothing.
  const uint32_t mode = abi::ReadU8(m, collection, qc::kOrganisationMode);
  if ((om & mode) == 0) {
    if (mode & rq::kOrganisationPassGroup) om = rq::kOrganisationPassGroup;
    else if (mode & rq::kOrganisationSortAscending) om = rq::kOrganisationSortAscending;
    else return;  // the guest throws here
  }
  if (om != rq::kOrganisationPassGroup && om != rq::kOrganisationSortDescending &&
      om != rq::kOrganisationSortAscending)
    return;
  const QueueReason scope = !main_scene ? QueueReason::kNotMainScene
                            : kind != QueueCollection::kSolidsBasic ? QueueReason::kNotSolidsBasic
                                                                    : QueueReason::kNative;
  std::vector<Unit> units;
  if (om == rq::kOrganisationPassGroup) {
    ForEachNode(m, collection + qc::kGrouped.offset, [&](uint32_t node) {
      const uint32_t pass = abi::ReadU32(m, node, abi::ogre::stl_tree::kNodeKey);
      const uint32_t list = abi::ReadU32(m, node, abi::ogre::stl_tree::kNodeValue);
      const uint32_t first = abi::ReadU32(m, list, abi::ogre::stl_vector::kFirst);
      const uint32_t last = abi::ReadU32(m, list, abi::ogre::stl_vector::kLast);
      if (first == last) return;  // the guest skips empty groups too (it walks nothing)
      Unit u;
      const QueueReason pass_reason = scope != QueueReason::kNative ? scope : ClassifyPass(m, pass);
      float lowest = 0;
      for (uint32_t i = first; i < last; i += 4) {
        const uint32_t rend = abi::ReadU32(m, i);
        const float key = abi::ReadF32(m, rend, rq::kRenderableGroupKey);
        if (i == first || key < lowest) lowest = key;
        AddObject(u, pass_reason != QueueReason::kNative ? pass_reason : ClassifyRenderable(m, rend));
      }
      // Runic's walk sorts by the key, ascending, and starts from -1.0: the group's head run has
      // no pass visit exactly when its lowest key is -1.0.
      u.from_minus_one = lowest == -1.0f;
      units.push_back(u);
    });
  } else {
    const uint32_t first = abi::ReadU32(m, collection, qc::kSortedDescending);
    const uint32_t last = abi::ReadU32(m, collection + qc::kSortedDescending.offset + 4);
    for (uint32_t i = first; i < last; i += qc::kRenderablePassSize) {
      const uint32_t rend = abi::ReadU32(m, i), pass = abi::ReadU32(m, i + 4);
      Unit u;
      QueueReason r = scope != QueueReason::kNative ? scope : ClassifyPass(m, pass);
      if (r == QueueReason::kNative) r = ClassifyRenderable(m, rend);
      AddObject(u, r);
      units.push_back(u);
    }
  }
  QueueCensusTotals t;
  t.collections = 1;
  t.ms = ms;
  uint64_t draws = 0, failing = 0, native_group = 0;
  for (const Unit& u : units) {
    draws += u.draws;
    failing += u.failing;
    if (om == rq::kOrganisationPassGroup) {
      ++t.groups;
      if (u.failing && u.failing < u.draws) ++t.mixed_groups;
      if (u.from_minus_one) ++t.groups_from_minus_one;
    }
    // Per pass group: a group whose head runs from -1.0 would draw with a pass from outside it,
    // so it stays with the guest here.
    if (!u.failing && !u.from_minus_one) native_group += u.draws;
    t.draws_by_reason[static_cast<size_t>(u.failing ? u.first_reason : QueueReason::kNative)] +=
        u.failing ? u.failing : u.draws;
    if (u.failing && u.failing < u.draws)
      t.draws_by_reason[static_cast<size_t>(QueueReason::kNative)] += u.draws - u.failing;
  }
  t.draws = draws;
  if (failing && failing < draws) t.mixed_collections = 1;
  // Whole collection: its first group's head run from -1.0 would draw with a pass from outside it.
  const bool first_from_minus_one = !units.empty() && units.front().from_minus_one;
  const bool whole = draws && !failing && !first_from_minus_one;
  t.draws_native_whole = whole ? draws : 0;
  t.ms_native_whole = whole ? ms : 0;
  t.draws_native_group = native_group;
  t.ms_native_group = draws ? ms * double(native_group) / double(draws) : 0;
  totals.Add(t);
}

}  // namespace torchlight::hooks
