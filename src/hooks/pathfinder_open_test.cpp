// The host neighbour opening (pathfinder_open.h) on a fake guest memory: a new node's values and
// its place in the sorted open list (before the first node whose f is not below), the parent's
// child slot (and the guest's slot 8 over `next`), the best node, and a cell already open or closed
// with and without a lower g. Only the planned words change.

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "guest_abi/ogre_layout.h"
#include "guest_abi/pathfinder.h"
#include "hooks/pathfinder_open.h"

namespace {

namespace abi = torchlight::guest_abi;
namespace pf = abi::pathfinder;
namespace node = abi::pathfinder::node;
using namespace torchlight::hooks;

int failures = 0;
void Check(bool ok, const char* what) {
  if (ok) return;
  ++failures;
  std::fprintf(stderr, "FAIL: %s\n", what);
}
void WriteF32(uint8_t* m, uint32_t addr, float v) { abi::WriteU32(m, addr, std::bit_cast<uint32_t>(v)); }
float F32(const uint8_t* m, uint32_t addr) { return abi::ReadF32(m, addr); }

constexpr uint32_t kSearch = 0x100, kOpenHead = 0x200, kClosedHead = 0x240, kParent = 0x300;
constexpr uint32_t kOpen1 = 0x400, kOpen2 = 0x440, kOpen3 = 0x480, kClosed1 = 0x500, kNew = 0x600;
constexpr float kStep = 1.5f;

// A node at `at` with f, g, h, cell and next.
void SetNode(uint8_t* m, uint32_t at, float f, float h, float g, uint32_t x, uint32_t y, uint32_t width, uint32_t next) {
  for (uint32_t i = 0; i < 64; i += 4) abi::WriteU32(m, at + i, 0);
  WriteF32(m, at + node::kF.offset, f);
  WriteF32(m, at + node::kH.offset, h);
  WriteF32(m, at + node::kG.offset, g);
  abi::WriteU32(m, at + node::kX.offset, x);
  abi::WriteU32(m, at + node::kY.offset, y);
  abi::WriteU32(m, at + node::kCell.offset, x + y * width);
  abi::WriteU32(m, at + node::kNext.offset, next);
}

std::vector<uint8_t> Scene() {
  std::vector<uint8_t> m(0x800);
  for (size_t i = 0; i < m.size(); ++i) m[i] = uint8_t(i * 7 + 1);
  const uint32_t width = 100;
  abi::WriteU32(m.data(), kSearch + pf::kWidth.offset, width);
  abi::WriteU32(m.data(), kSearch + pf::kOpenHead.offset, kOpenHead);
  abi::WriteU32(m.data(), kSearch + pf::kClosedHead.offset, kClosedHead);
  abi::WriteU32(m.data(), kSearch + pf::kBest.offset, kOpen1);
  SetNode(m.data(), kOpenHead, 0, 0, 0, 0, 0, width, kOpen1);
  SetNode(m.data(), kClosedHead, 0, 0, 0, 0, 0, width, kClosed1);
  SetNode(m.data(), kOpen1, 10.0f, 4.0f, 6.0f, 1, 1, width, kOpen2);
  SetNode(m.data(), kOpen2, 20.0f, 8.0f, 12.0f, 2, 2, width, kOpen3);
  SetNode(m.data(), kOpen3, 30.0f, 10.0f, 20.0f, 3, 3, width, 0);
  SetNode(m.data(), kClosed1, 5.0f, 1.0f, 4.0f, 5, 5, width, 0);
  SetNode(m.data(), kParent, 9.0f, 3.0f, 6.0f, 7, 7, width, 0);
  for (uint32_t i = 0; i < 64; i += 4) abi::WriteU32(m.data(), kNew + i, 0);
  return m;
}

size_t Changed(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
  size_t n = 0;
  for (size_t i = 0; i < a.size(); i += 4)
    if (a[i] != b[i] || a[i + 1] != b[i + 1] || a[i + 2] != b[i + 2] || a[i + 3] != b[i + 3]) ++n;
  return n;
}

}  // namespace

int main() {
  {
    // A new cell (8, 7) toward the goal (10, 10): h = 4 + 9 = 13, g = 6 + 1.5, f = 20.5: after the
    // node with f 20 (not below 20.5? 20 < 20.5, so after it), before f 30.
    std::vector<uint8_t> m = Scene(), before = m;
    OpenPlan p = PlanOpenNeighbour(m.data(), kSearch, kParent, 8, 7, 10, 10, kStep);
    Check(p.what == OpenPlan::Case::kNew, "new cell");
    Check(p.cell == 8 + 7 * 100, "cell index");
    Check(p.g == 7.5f && p.h == 13.0f && p.f == 20.5f, "g, h and f");
    Check(p.predecessor == kOpen2 && p.successor == kOpen3, "placed between f 20 and f 30");
    Check(p.slot == 0 && !p.new_best, "first child slot; the best (h 4) stays");
    Check(m == before, "the plan writes nothing");
    Check(ApplyOpenPlan(m.data(), kSearch, kParent, 8, 7, p, kNew) == 0, "nothing to improve");
    Check(abi::ReadU32(m.data(), kOpen2 + node::kNext.offset) == kNew && abi::ReadU32(m.data(), kNew + node::kNext.offset) == kOpen3,
          "linked into the open list");
    Check(F32(m.data(), kNew + node::kF.offset) == 20.5f && abi::ReadU32(m.data(), kNew + node::kParent.offset) == kParent &&
              abi::ReadU32(m.data(), kNew + node::kX.offset) == 8 && abi::ReadU32(m.data(), kNew + node::kY.offset) == 7 &&
              abi::ReadU32(m.data(), kNew + node::kCell.offset) == 708,
          "the new node's fields");
    Check(abi::ReadU32(m.data(), kParent + node::kChildren.offset) == kNew, "the parent's first child");
    Check(Changed(before, m) == 10, "ten words written: eight fields, the predecessor's next, the child slot");
  }
  {
    // A new cell at the goal: h 0 makes it the best; f 7.5 goes first in the list (10 is not below).
    std::vector<uint8_t> m = Scene();
    OpenPlan p = PlanOpenNeighbour(m.data(), kSearch, kParent, 10, 10, 10, 10, kStep);
    Check(p.what == OpenPlan::Case::kNew && p.h == 0.0f && p.f == 7.5f, "the goal cell");
    Check(p.predecessor == kOpenHead && p.successor == kOpen1 && p.new_best, "first in the list, the new best");
    ApplyOpenPlan(m.data(), kSearch, kParent, 10, 10, p, kNew);
    Check(abi::ReadU32(m.data(), kSearch + pf::kBest.offset) == kNew, "best updated");
    Check(abi::ReadU32(m.data(), kOpenHead + node::kNext.offset) == kNew, "head points at it");
  }
  {
    // Equal f: the new node goes before the node with the same f (its f is not below).
    std::vector<uint8_t> m = Scene();
    WriteF32(m.data(), kParent + node::kG.offset, 10.0f - kStep);  // g 10, h 10 at (9, 7) -> f 20
    OpenPlan p = PlanOpenNeighbour(m.data(), kSearch, kParent, 9, 7, 10, 10, kStep);
    Check(p.what == OpenPlan::Case::kNew && p.f == 20.0f && p.predecessor == kOpen1 && p.successor == kOpen2, "ties go first");
    // An empty open list: linked to the head, next left at 0.
    abi::WriteU32(m.data(), kOpenHead + node::kNext.offset, 0);
    abi::WriteU32(m.data(), kSearch + pf::kBest.offset, 0);
    p = PlanOpenNeighbour(m.data(), kSearch, kParent, 9, 7, 10, 10, kStep);
    Check(p.predecessor == kOpenHead && p.successor == 0 && p.new_best, "empty list: after the head, the best");
  }
  {
    // All eight child slots taken: the guest stores at slot 8, over the parent's next.
    std::vector<uint8_t> m = Scene();
    for (uint32_t i = 0; i < 8; ++i) abi::WriteU32(m.data(), kParent + node::kChildren.offset + 4 * i, 0x700 + i);
    OpenPlan p = PlanOpenNeighbour(m.data(), kSearch, kParent, 8, 7, 10, 10, kStep);
    Check(p.slot == 8, "no free slot");
    ApplyOpenPlan(m.data(), kSearch, kParent, 8, 7, p, kNew);
    Check(abi::ReadU32(m.data(), kParent + node::kNext.offset) == kNew, "slot 8 is the parent's next, as the guest writes it");
  }
  {
    // The cell (2, 2) is open with g 12: through the parent g is 7.5, lower -> parent, g and f change.
    std::vector<uint8_t> m = Scene(), before = m;
    OpenPlan p = PlanOpenNeighbour(m.data(), kSearch, kParent, 2, 2, 10, 10, kStep);
    Check(p.what == OpenPlan::Case::kOpen && p.node == kOpen2 && p.improves && p.f == 8.0f + 7.5f, "open cell improved");
    Check(ApplyOpenPlan(m.data(), kSearch, kParent, 2, 2, p, 0) == 0, "an open node needs no guest call");
    Check(F32(m.data(), kOpen2 + node::kG.offset) == 7.5f && F32(m.data(), kOpen2 + node::kF.offset) == 15.5f &&
              abi::ReadU32(m.data(), kOpen2 + node::kParent.offset) == kParent,
          "the open node's parent, g and f");
    Check(abi::ReadU32(m.data(), kOpen2 + node::kNext.offset) == kOpen3, "the list is not re-sorted");
    Check(Changed(before, m) == 4, "four words: three fields and the child slot");
    // Not lower: only the child slot.
    m = Scene();
    WriteF32(m.data(), kParent + node::kG.offset, 100.0f);
    before = m;
    p = PlanOpenNeighbour(m.data(), kSearch, kParent, 2, 2, 10, 10, kStep);
    Check(p.what == OpenPlan::Case::kOpen && !p.improves, "open cell not improved");
    ApplyOpenPlan(m.data(), kSearch, kParent, 2, 2, p, 0);
    Check(Changed(before, m) == 1 && abi::ReadU32(m.data(), kParent + node::kChildren.offset) == kOpen2, "only the child slot");
  }
  {
    // The cell (5, 5) is closed with g 4: through the parent g is 7.5, not lower. With a cheaper
    // parent it improves and the guest's propagation is asked for.
    std::vector<uint8_t> m = Scene();
    OpenPlan p = PlanOpenNeighbour(m.data(), kSearch, kParent, 5, 5, 10, 10, kStep);
    Check(p.what == OpenPlan::Case::kClosed && p.node == kClosed1 && !p.improves, "closed cell, no improvement");
    Check(ApplyOpenPlan(m.data(), kSearch, kParent, 5, 5, p, 0) == 0, "no propagation");
    WriteF32(m.data(), kParent + node::kG.offset, 1.0f);
    p = PlanOpenNeighbour(m.data(), kSearch, kParent, 5, 5, 10, 10, kStep);
    Check(p.improves && p.g == 2.5f && p.f == 3.5f, "closed cell improved");
    Check(ApplyOpenPlan(m.data(), kSearch, kParent, 5, 5, p, 0) == kClosed1, "the closed node goes to the guest's propagation");
    Check(F32(m.data(), kClosed1 + node::kG.offset) == 2.5f && F32(m.data(), kClosed1 + node::kF.offset) == 3.5f, "its g and f");
  }
  {
    // The squared distance is formed as the guest forms it: float differences, dx*dx rounded, then
    // the fused dy*dy; and negative differences.
    std::vector<uint8_t> m = Scene();
    OpenPlan p = PlanOpenNeighbour(m.data(), kSearch, kParent, 8, 7, 16777217u, 10, kStep);
    const double dx = float(double(int32_t(8u - 16777217u))), dy = float(3.0 - 0.0) * -1;
    Check(p.h == float(std::fma(dy, dy, double(float(dx * dx)))), "the guest's order for h");
  }
  if (failures == 0) std::printf("pathfinder_open_test: ok\n");
  return failures == 0 ? 0 : 1;
}
