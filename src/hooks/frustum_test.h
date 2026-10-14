// Frustum::isVisible(const AxisAlignedBox&) as Runic's build compiles it (0x821C74A0, the culling's
// node and object test), on the host (docs/game-thread-parallelism.md, section 12). The guest
// function is straight-line VMX code with no calls: the box's extent decides the null and infinite
// cases, a scalar part forms min+max and min-max per axis, and a vector part tests the six planes.
// The host version does the same reads and the same operations in the same order, the vector part
// with the SIMDe functions the recompiled code itself uses and in its lane order, so the result is
// the guest's bit for bit. Pure over a guest memory base, tested on a fake memory.

#pragma once

#include <cstdint>

namespace torchlight::hooks {

// The rodata floats the guest's test reads (guest_abi static_geometry::frustum::kBoxScale,
// kBoxSumW, kBoxDifferenceW).
struct FrustumTestConstants {
  float scale;
  float sum_w;
  float difference_w;
};

// The scalar part: the box's minimum+maximum and minimum-maximum per axis, each a double sum
// rounded to float as the recompiled code computes it. Runs in the guest's scalar FPU mode (flush
// to zero off).
struct BoxSums {
  float sum[3];
  float difference[3];
};

// The box's extent decides without arithmetic: null is not visible, infinite is visible. Returns
// true when `visible` holds the answer.
bool BoxExtentDecides(const uint8_t* membase, uint32_t box, bool* visible);

BoxSums ComputeBoxSums(const uint8_t* membase, uint32_t box);

// The vector part: false when the box lies entirely outside one of the six planes at
// frustum + static_geometry::frustum::kPlanes, tested in the guest's order. Runs in the guest's
// VMX mode (flush to zero on).
bool PlanesAdmit(const uint8_t* membase, uint32_t frustum, const BoxSums& sums,
                 const FrustumTestConstants& constants);

}  // namespace torchlight::hooks
