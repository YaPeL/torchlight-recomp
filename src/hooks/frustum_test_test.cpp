// The host frustum test (frustum_test.h) on a fake guest memory: the extent's early answers, the
// scalar sums as the guest rounds them, and the plane test against a scalar reference on boxes
// inside, outside, straddling and exactly touching a plane. Reads only: the memory is unchanged.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "guest_abi/ogre_layout.h"
#include "guest_abi/static_geometry.h"
#include "hooks/frustum_test.h"

namespace {

namespace abi = torchlight::guest_abi;
namespace box = abi::static_geometry::axis_aligned_box;
namespace fr = abi::static_geometry::frustum;
using namespace torchlight::hooks;

int failures = 0;
void Check(bool ok, const char* what) {
  if (ok) return;
  ++failures;
  std::fprintf(stderr, "FAIL: %s\n", what);
}

void WriteF32(uint8_t* m, uint32_t addr, float v) { abi::WriteU32(m, addr, std::bit_cast<uint32_t>(v)); }

constexpr uint32_t kFrustum = 0x1000, kBox = 0x2000;
// The guest's constants as the test expects them: half the sums gives the centre with w 1 and the
// negated half size with w 0.
constexpr FrustumTestConstants kConstants{0.5f, 2.0f, 0.0f};

void SetBox(uint8_t* m, float lo[3], float hi[3], uint32_t extent) {
  for (uint32_t a = 0; a < 3; ++a) {
    WriteF32(m, kBox + box::kMinimum.offset + 4 * a, lo[a]);
    WriteF32(m, kBox + box::kMaximum.offset + 4 * a, hi[a]);
  }
  abi::WriteU32(m, kBox + box::kExtent.offset, extent);
}

// A frustum of six axis planes, normals inward: the cube [-10, 10]^3.
void SetCubeFrustum(uint8_t* m) {
  const float planes[6][4] = {{1, 0, 0, 10}, {-1, 0, 0, 10}, {0, 1, 0, 10},
                              {0, -1, 0, 10}, {0, 0, 1, 10}, {0, 0, -1, 10}};
  for (uint32_t i = 0; i < 6; ++i)
    for (uint32_t k = 0; k < 4; ++k) WriteF32(m, kFrustum + fr::kPlanes.offset + 16 * i + 4 * k, planes[i][k]);
}

// The reference: OGRE's test, outside when the centre's distance is below minus the box's extent
// along the normal (strictly).
bool Reference(const uint8_t* m, float lo[3], float hi[3]) {
  for (uint32_t i = 0; i < 6; ++i) {
    float n[4];
    for (uint32_t k = 0; k < 4; ++k) n[k] = abi::ReadF32(m, kFrustum + fr::kPlanes.offset + 16 * i + 4 * k);
    float distance = n[3], extent = 0;
    for (uint32_t a = 0; a < 3; ++a) {
      distance += n[a] * (lo[a] + hi[a]) * 0.5f;
      extent += std::fabs(n[a]) * (hi[a] - lo[a]) * 0.5f;
    }
    if (distance < -extent) return false;
  }
  return true;
}

bool Visible(const uint8_t* m) {
  bool visible;
  if (BoxExtentDecides(m, kBox, &visible)) return visible;
  return PlanesAdmit(m, kFrustum, ComputeBoxSums(m, kBox), kConstants);
}

}  // namespace

int main() {
  std::vector<uint8_t> m(0x3000);
  for (size_t i = 0; i < m.size(); ++i) m[i] = uint8_t(i * 7 + 1);
  SetCubeFrustum(m.data());

  float lo[3] = {-1, -1, -1}, hi[3] = {1, 1, 1};
  SetBox(m.data(), lo, hi, box::kExtentNull);
  Check(!Visible(m.data()), "a null box is not visible");
  SetBox(m.data(), lo, hi, box::kExtentInfinite);
  Check(Visible(m.data()), "an infinite box is visible");
  SetBox(m.data(), lo, hi, box::kExtentFinite);
  Check(Visible(m.data()), "a box inside the frustum");
  const BoxSums s = ComputeBoxSums(m.data(), kBox);
  Check(s.sum[0] == 0.0f && s.sum[2] == 0.0f && s.difference[1] == -2.0f, "the sums are min+max and min-max");

  struct Case {
    float lo[3], hi[3];
    bool visible;
    const char* what;
  } cases[] = {
      {{11, -1, -1}, {12, 1, 1}, false, "entirely beyond +x"},
      {{-12, -1, -1}, {-11, 1, 1}, false, "entirely beyond -x"},
      {{-1, -1, 11}, {1, 1, 12}, false, "entirely beyond +z"},
      {{9, -1, -1}, {11, 1, 1}, true, "straddling +x"},
      {{10, -1, -1}, {12, 1, 1}, true, "touching +x exactly (strict compare)"},
      {{10.5f, -1, -1}, {12, 1, 1}, false, "just beyond +x"},
      {{-1, -1, -1}, {1, 1, -10}, true, "touching -z from inside (flipped extents, reference agrees)"},
      {{-100, -100, -100}, {100, 100, 100}, true, "a box containing the frustum"},
      {{-1, 15, -1}, {1, 16, 1}, false, "entirely beyond +y"},
  };
  for (Case& c : cases) {
    SetBox(m.data(), c.lo, c.hi, box::kExtentFinite);
    const bool got = Visible(m.data());
    Check(got == c.visible, c.what);
    Check(got == Reference(m.data(), c.lo, c.hi), "agrees with the scalar reference");
  }

  // Rotated planes: a normal with three components, so the lane order of the dot products counts.
  const float n = 1.0f / std::sqrt(3.0f);
  for (uint32_t k = 0; k < 3; ++k) WriteF32(m.data(), kFrustum + fr::kPlanes.offset + 4 * k, n);
  WriteF32(m.data(), kFrustum + fr::kPlanes.offset + 12, 0.0f);  // x + y + z >= 0
  float lo2[3] = {-3, -3, -3}, hi2[3] = {-2, -2, -2};
  SetBox(m.data(), lo2, hi2, box::kExtentFinite);
  Check(!Visible(m.data()) && !Reference(m.data(), lo2, hi2), "a box behind the diagonal plane");
  float lo3[3] = {-3, -3, -3}, hi3[3] = {1, 1, 1};
  SetBox(m.data(), lo3, hi3, box::kExtentFinite);
  Check(Visible(m.data()) && Reference(m.data(), lo3, hi3), "a box crossing the diagonal plane");

  // Reads only.
  std::vector<uint8_t> before = m;
  Visible(m.data());
  Check(before == m, "the test writes nothing");

  if (failures == 0) std::printf("frustum_test_test: ok\n");
  return failures == 0 ? 0 : 1;
}
