// The pathfinder's neighbour opening (guest_abi/pathfinder.h, kOpenNeighbour) as the recompiled
// code does it, on the host (docs/game-thread-parallelism.md, section 12). Two steps so the plan
// can be checked against the guest: PlanOpenNeighbour reads the lists and decides everything the
// guest would write (no writes); ApplyOpenPlan writes it, in the guest's order. The float values
// are formed as the generated code forms them (double operations rounded to float, the fused
// multiply-add of the squared distance). Pure over a guest memory base, tested on a fake memory.

#pragma once

#include <cstdint>

namespace torchlight::hooks {

struct OpenPlan {
  enum class Case : uint8_t { kNew, kOpen, kClosed };
  Case what = Case::kNew;
  uint32_t node = 0;         // kOpen, kClosed: the node holding the cell
  uint32_t cell = 0;         // x + y * width, as the guest computes it (32 bits)
  float g = 0;               // the parent's g plus the step cost
  float h = 0;               // kNew: the squared distance to the goal
  float f = 0;               // kNew: h + g; kOpen, kClosed when `improves`: the node's new f
  bool improves = false;     // kOpen, kClosed: g is below the node's (its parent, g and f change)
  uint32_t predecessor = 0;  // kNew: the open node (or the open head) the new node follows
  uint32_t successor = 0;    // kNew: the open node after it, 0 at the end
  uint32_t slot = 0;         // the parent's first free child slot (8 when none is free)
  bool new_best = false;     // kNew: the search's best becomes the new node
};

OpenPlan PlanOpenNeighbour(const uint8_t* membase, uint32_t search, uint32_t parent, uint32_t x,
                           uint32_t y, uint32_t goal_x, uint32_t goal_y, float step_cost);

// Writes the plan. `node` is the new node for kNew (64 zeroed bytes from the game's allocator),
// ignored otherwise. Returns the closed node whose g improved, for the guest's kImproveClosed,
// or 0.
uint32_t ApplyOpenPlan(uint8_t* membase, uint32_t search, uint32_t parent, uint32_t x, uint32_t y,
                       const OpenPlan& plan, uint32_t node);

}  // namespace torchlight::hooks
