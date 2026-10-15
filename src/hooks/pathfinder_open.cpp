#include "hooks/pathfinder_open.h"

#include <bit>
#include <cmath>

#include "guest_abi/ogre_layout.h"
#include "guest_abi/pathfinder.h"

namespace torchlight::hooks {

namespace {

namespace abi = torchlight::guest_abi;
namespace pf = abi::pathfinder;
namespace node = abi::pathfinder::node;

uint32_t FindCell(const uint8_t* m, uint32_t head, uint32_t cell) {
  for (uint32_t n = abi::ReadU32(m, head, node::kNext); n != 0; n = abi::ReadU32(m, n, node::kNext))
    if (abi::ReadU32(m, n, node::kCell) == cell) return n;
  return 0;
}

// The first child slot of `parent` holding 0, or kChildSlots.
uint32_t FreeSlot(const uint8_t* m, uint32_t parent) {
  uint32_t slot = 0;
  while (slot < node::kChildSlots && abi::ReadU32(m, parent + node::kChildren.offset + 4 * slot) != 0) ++slot;
  return slot;
}

void WriteF32(uint8_t* m, uint32_t addr, float v) { abi::WriteU32(m, addr, std::bit_cast<uint32_t>(v)); }

}  // namespace

OpenPlan PlanOpenNeighbour(const uint8_t* m, uint32_t search, uint32_t parent, uint32_t x, uint32_t y,
                           uint32_t goal_x, uint32_t goal_y, float step_cost) {
  OpenPlan plan;
  const int64_t width = int32_t(abi::ReadU32(m, search, pf::kWidth));
  plan.cell = uint32_t(width * int64_t(int32_t(y)) + int64_t(x));
  plan.g = float(double(abi::ReadF32(m, parent, node::kG)) + double(step_cost));
  plan.slot = FreeSlot(m, parent);
  if (uint32_t n = FindCell(m, abi::ReadU32(m, search, pf::kOpenHead), plan.cell)) {
    plan.what = OpenPlan::Case::kOpen;
    plan.node = n;
  } else if (uint32_t c = FindCell(m, abi::ReadU32(m, search, pf::kClosedHead), plan.cell)) {
    plan.what = OpenPlan::Case::kClosed;
    plan.node = c;
  }
  if (plan.what != OpenPlan::Case::kNew) {
    plan.improves = double(plan.g) < double(abi::ReadF32(m, plan.node, node::kG));
    if (plan.improves) plan.f = float(double(abi::ReadF32(m, plan.node, node::kH)) + double(plan.g));
    return plan;
  }
  // The squared distance to the goal: the differences as 32-bit integers, each to float, dy*dy
  // fused onto float(dx*dx).
  const double dy = float(double(int32_t(y - goal_y)));
  const double dx = float(double(int32_t(x - goal_x)));
  const double dx2 = float(dx * dx);
  plan.h = float(std::fma(dy, dy, dx2));
  plan.f = float(double(plan.h) + double(plan.g));
  // The open list is sorted by f: the new node goes before the first node whose f is not below.
  plan.predecessor = abi::ReadU32(m, search, pf::kOpenHead);
  uint32_t n = abi::ReadU32(m, plan.predecessor, node::kNext);
  while (n != 0 && double(abi::ReadF32(m, n, node::kF)) < double(plan.f)) {
    plan.predecessor = n;
    n = abi::ReadU32(m, n, node::kNext);
  }
  plan.successor = n;
  const uint32_t best = abi::ReadU32(m, search, pf::kBest);
  plan.new_best = best == 0 || double(abi::ReadF32(m, best, node::kH)) > double(plan.h);
  return plan;
}

uint32_t ApplyOpenPlan(uint8_t* m, uint32_t search, uint32_t parent, uint32_t x, uint32_t y,
                       const OpenPlan& plan, uint32_t node_address) {
  const uint32_t child_slot = parent + node::kChildren.offset + 4 * plan.slot;
  if (plan.what == OpenPlan::Case::kNew) {
    const uint32_t n = node_address;
    WriteF32(m, n + node::kG.offset, plan.g);
    abi::WriteU32(m, n + node::kParent.offset, parent);
    abi::WriteU32(m, n + node::kX.offset, x);
    abi::WriteU32(m, n + node::kY.offset, y);
    abi::WriteU32(m, n + node::kCell.offset, plan.cell);
    WriteF32(m, n + node::kH.offset, plan.h);
    WriteF32(m, n + node::kF.offset, plan.f);
    if (plan.successor != 0) abi::WriteU32(m, n + node::kNext.offset, plan.successor);
    abi::WriteU32(m, plan.predecessor + node::kNext.offset, n);
    if (plan.new_best) abi::WriteU32(m, search + pf::kBest.offset, n);
    abi::WriteU32(m, child_slot, n);
    return 0;
  }
  abi::WriteU32(m, child_slot, plan.node);
  if (!plan.improves) return 0;
  abi::WriteU32(m, plan.node + node::kParent.offset, parent);
  WriteF32(m, plan.node + node::kG.offset, plan.g);
  WriteF32(m, plan.node + node::kF.offset, plan.f);
  return plan.what == OpenPlan::Case::kClosed ? plan.node : 0;
}

}  // namespace torchlight::hooks
