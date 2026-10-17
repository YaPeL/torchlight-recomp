#include "hooks/frustum_test.h"

#include <simde/x86/sse4.1.h>

#include "guest_abi/ogre_layout.h"
#include "guest_abi/static_geometry.h"

namespace torchlight::hooks {

namespace {

namespace abi = torchlight::guest_abi;
namespace box = abi::static_geometry::axis_aligned_box;
namespace fr = abi::static_geometry::frustum;

// A guest float4 as the recompiled code holds it after its byte-reversing load: the guest's first
// element in lane 3, its fourth in lane 0. The dot products below sum the lanes in that order.
simde__m128 GuestLanes(float x, float y, float z, float w) { return simde_mm_set_ps(x, y, z, w); }

}  // namespace

bool BoxExtentDecides(const uint8_t* membase, uint32_t b, bool* visible) {
  const uint32_t extent = abi::ReadU32(membase, b, box::kExtent);
  if (extent == box::kExtentNull) {
    *visible = false;
    return true;
  }
  if (extent == box::kExtentInfinite) {
    *visible = true;
    return true;
  }
  return false;
}

BoxSums ComputeBoxSums(const uint8_t* membase, uint32_t b) {
  BoxSums s;
  for (uint32_t axis = 0; axis < 3; ++axis) {
    const double lo = abi::ReadF32(membase, b + box::kMinimum.offset + 4 * axis);
    const double hi = abi::ReadF32(membase, b + box::kMaximum.offset + 4 * axis);
    s.sum[axis] = float(lo + hi);
    s.difference[axis] = float(lo - hi);
  }
  return s;
}

bool PlanesAdmit(const uint8_t* membase, uint32_t frustum, const BoxSums& s,
                 const FrustumTestConstants& c) {
  const simde__m128 zero = simde_mm_setzero_ps();
  const simde__m128 scale = simde_mm_set1_ps(c.scale);
  // With the guest's constants these are the box's centre (w 1) and its negated half size (w 0).
  const simde__m128 centre = simde_mm_mul_ps(GuestLanes(s.sum[0], s.sum[1], s.sum[2], c.sum_w), scale);
  const simde__m128 reach = simde_mm_mul_ps(
      GuestLanes(s.difference[0], s.difference[1], s.difference[2], c.difference_w), scale);
  for (uint32_t i = 0; i < fr::kPlaneCount; ++i) {
    const uint32_t p = frustum + fr::kPlanes.offset + 16 * i;
    const simde__m128 plane = GuestLanes(abi::ReadF32(membase, p), abi::ReadF32(membase, p + 4),
                                         abi::ReadF32(membase, p + 8), abi::ReadF32(membase, p + 12));
    const simde__m128 magnitude = simde_mm_max_ps(plane, simde_mm_sub_ps(zero, plane));
    // Four-lane dot: the plane's distance to the centre. Three-lane dot (the guest's fourth element
    // left out): minus the box's extent along the plane's normal. Outside when the latter is larger.
    const simde__m128 distance = simde_mm_dp_ps(plane, centre, 0xFF);
    const simde__m128 extent = simde_mm_dp_ps(magnitude, reach, 0xEF);
    if (simde_mm_movemask_ps(simde_mm_cmpgt_ps(extent, distance)) == 0xF) return false;
  }
  return true;
}

}  // namespace torchlight::hooks
