// The stage 0 queue census (queue_census.h) on a fake guest memory: a pass-grouped collection and a
// sorted one, built with the guest's layouts.

#include <cstdint>
#include <cstdio>
#include <vector>

#include "guest_abi/ogre_layout.h"
#include "guest_abi/render_queue.h"
#include "hooks/queue_census.h"

namespace {

namespace abi = torchlight::guest_abi;
namespace rq = abi::render_queue;
using namespace torchlight::hooks;

int failures = 0;
void Check(bool ok, const char* what) {
  if (ok) return;
  ++failures;
  std::fprintf(stderr, "FAIL: %s\n", what);
}

struct Memory {
  std::vector<uint8_t> m = std::vector<uint8_t>(0x10000);
  uint32_t next = 0x100;
  uint32_t Alloc(uint32_t size) {
    const uint32_t a = next;
    next += (size + 15) & ~15u;
    return a;
  }
  void U32(uint32_t a, uint32_t v) { abi::WriteU32(m.data(), a, v); }
  void F32(uint32_t a, float v) {
    uint32_t u;
    __builtin_memcpy(&u, &v, 4);
    U32(a, u);
  }
  void U8(uint32_t a, uint8_t v) { m[a] = v; }
  void U16(uint32_t a, uint16_t v) { abi::WriteU16(m.data(), a, v); }
};

uint32_t MakePass(Memory& g, bool good) {
  namespace p = rq::pass;
  const uint32_t pass = g.Alloc(0x180);
  g.U32(pass + p::kVertexProgramUsage.offset, 0x9000);
  g.U32(pass + p::kFragmentProgramUsage.offset, good ? 0x9100 : 0);
  g.U32(pass + p::kPassIterationCount.offset, 1);
  return pass;
}
uint32_t MakeSubEntity(Memory& g, bool skinned, float key) {
  const uint32_t entity = g.Alloc(0x200);
  g.U16(entity + rq::entity::kNumBoneMatrices.offset, skinned ? 40 : 0);
  const uint32_t rend = g.Alloc(0x80);
  g.U32(rend, rq::renderable_class::kSubEntityVtable);
  g.U32(rend + rq::sub_entity::kParentEntity.offset, entity);
  g.F32(rend + rq::kRenderableGroupKey.offset, key);
  return rend;
}

// A pass-grouped collection with one map node per group: {pass, renderables}.
uint32_t MakeGrouped(Memory& g, const std::vector<std::pair<uint32_t, std::vector<uint32_t>>>& groups) {
  namespace qc = rq::queued_renderable_collection;
  namespace t = abi::ogre::stl_tree;
  const uint32_t coll = g.Alloc(qc::kSize.bytes);
  g.U8(coll + qc::kOrganisationMode.offset, rq::kOrganisationPassGroup);
  const uint32_t head = g.Alloc(0x18);
  g.U32(coll + qc::kGrouped.offset + t::kHead.offset, head);
  // A right-leaning chain: head->left = first, each node's right = the next, parent links back.
  uint32_t prev = head;
  std::vector<uint32_t> nodes;
  for (const auto& [pass, rends] : groups) {
    const uint32_t list = g.Alloc(0x10), items = g.Alloc(4 * uint32_t(rends.size()) + 4);
    for (size_t i = 0; i < rends.size(); ++i) g.U32(items + 4 * uint32_t(i), rends[i]);
    g.U32(list + abi::ogre::stl_vector::kFirst.offset, items);
    g.U32(list + abi::ogre::stl_vector::kLast.offset, items + 4 * uint32_t(rends.size()));
    const uint32_t node = g.Alloc(0x18);
    g.U32(node + t::kNodeLeft.offset, head);
    g.U32(node + t::kNodeRight.offset, head);
    g.U32(node + t::kNodeParent.offset, prev);
    g.U32(node + t::kNodeKey.offset, pass);
    g.U32(node + t::kNodeValue.offset, list);
    if (prev == head) g.U32(head + t::kNodeLeft.offset, node);
    else g.U32(prev + t::kNodeRight.offset, node);
    prev = node;
    nodes.push_back(node);
  }
  if (nodes.empty()) g.U32(head + t::kNodeLeft.offset, head);
  return coll;
}

}  // namespace

int main() {
  Memory g;
  const uint32_t good = MakePass(g, true), bad = MakePass(g, false);
  Check(ClassifyPass(g.m.data(), good) == QueueReason::kNative, "a pass with both programs");
  Check(ClassifyPass(g.m.data(), bad) == QueueReason::kNoPrograms, "no fragment program");
  const uint32_t still = MakeSubEntity(g, false, 0.5f), skinned = MakeSubEntity(g, true, 0.5f);
  Check(ClassifyRenderable(g.m.data(), still) == QueueReason::kNative, "a static sub-entity");
  Check(ClassifyRenderable(g.m.data(), skinned) == QueueReason::kSkinned, "a skinned sub-entity");

  // Three groups: all native; mixed (static + skinned); native but its head runs from -1.0.
  const uint32_t from_minus_one = MakeSubEntity(g, false, -1.0f);
  const uint32_t coll = MakeGrouped(g, {{good, {still, still}},
                                        {good, {still, skinned}},
                                        {good, {from_minus_one, still}}});
  QueueCensusTotals t;
  ClassifyCollection(g.m.data(), coll, rq::kOrganisationPassGroup, true,
                     QueueCollection::kSolidsBasic, 6.0, t);
  Check(t.collections == 1 && t.mixed_collections == 1, "the collection mixes");
  Check(t.groups == 3 && t.mixed_groups == 1, "one mixed group of three");
  Check(t.groups_from_minus_one == 1, "one group from -1.0");
  Check(t.draws == 6, "six draws");
  Check(t.draws_native_whole == 0 && t.ms_native_whole == 0, "whole-collection fallback: none");
  Check(t.draws_native_group == 2, "per group: only the first group");
  Check(t.ms_native_group == 2.0, "its share of the time");
  Check(t.draws_by_reason[size_t(QueueReason::kSkinned)] == 1, "one skinned draw");

  // The same collection in a projector pass: nothing native, every draw counted as projector.
  QueueCensusTotals p;
  ClassifyCollection(g.m.data(), coll, rq::kOrganisationPassGroup, false,
                     QueueCollection::kSolidsBasic, 1.0, p);
  Check(p.draws_native_group == 0 && p.draws_by_reason[size_t(QueueReason::kNotMainScene)] == 6,
        "a projector pass stays with the guest");

  // An all-native collection: native whole.
  const uint32_t clean = MakeGrouped(g, {{good, {still}}, {good, {still, still}}});
  QueueCensusTotals c;
  ClassifyCollection(g.m.data(), clean, rq::kOrganisationPassGroup, true,
                     QueueCollection::kSolidsBasic, 3.0, c);
  Check(c.draws_native_whole == 3 && c.ms_native_whole == 3.0 && c.mixed_collections == 0,
        "a clean collection is native whole");

  // A sorted (descending) collection of transparents: entries, none native.
  namespace qc = rq::queued_renderable_collection;
  const uint32_t sorted = g.Alloc(qc::kSize.bytes), entries = g.Alloc(16);
  g.U8(sorted + qc::kOrganisationMode.offset, rq::kOrganisationSortDescending);
  g.U32(entries, still);
  g.U32(entries + 4, good);
  g.U32(entries + 8, still);
  g.U32(entries + 12, good);
  g.U32(sorted + qc::kSortedDescending.offset, entries);
  g.U32(sorted + qc::kSortedDescending.offset + 4, entries + 16);
  QueueCensusTotals s;
  ClassifyCollection(g.m.data(), sorted, rq::kOrganisationSortDescending, true,
                     QueueCollection::kTransparents, 2.0, s);
  Check(s.draws == 2 && s.groups == 0 && s.draws_native_group == 0 &&
            s.draws_by_reason[size_t(QueueReason::kNotSolidsBasic)] == 2,
        "transparents stay with the guest");

  if (failures == 0) std::printf("queue_census_test: ok\n");
  return failures == 0 ? 0 : 1;
}
