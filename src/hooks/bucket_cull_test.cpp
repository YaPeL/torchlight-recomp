// The bucket culling's host-side logic (bucket_cull.h).

#include <cmath>
#include <cstdint>
#include <cstdio>

#include "hooks/bucket_cull.h"

namespace {

using namespace torchlight::hooks;

int failures = 0;

void Check(bool ok, const char* what) {
  if (ok) return;
  ++failures;
  std::fprintf(stderr, "FAIL: %s\n", what);
}

bool Near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

// A box frustum x, y, z in [-10, 10] as six inward planes (near/far along z first, as OGRE).
Planes Cube() {
  return Planes{{{0, 0, 1, 10}, {0, 0, -1, 10}, {1, 0, 0, 10}, {-1, 0, 0, 10}, {0, -1, 0, 10},
                 {0, 1, 0, 10}}};
}

Box At(float x, float y, float z, float h) { return Box{{x - h, y - h, z - h}, {x + h, y + h, z + h}}; }

}  // namespace

int main() {
  const Planes cube = Cube();
  Check(BoxVisible(cube, At(0, 0, 0, 1)), "inside");
  Check(BoxVisible(cube, At(10.5f, 0, 0, 1)), "straddling an edge counts as visible");
  Check(!BoxVisible(cube, At(12, 0, 0, 1)), "entirely past one plane");
  Check(!BoxVisible(cube, At(0, 0, -30, 5)), "entirely behind");
  Check(BoxVisible(cube, At(0, 0, 0, 100)), "a box holding the whole frustum stays");

  BoxBuilder b;
  Check(!b.Result(), "no positions: no box");
  b.Add(1, 2, 3);
  b.Add(-1, 5, 0);
  auto r = b.Result();
  Check(r && Near(r->min[0], -1) && Near(r->max[1], 5) && Near(r->min[2], 0), "box of positions");
  BoxBuilder nan;
  nan.Add(NAN, 0, 0);
  Check(!nan.Result(), "a non-finite position gives no box (never culled)");

  // Translation by (100, 0, 0) and a quarter turn about y: x -> -z, z -> x.
  const Matrix move{1, 0, 0, 100, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  Box moved = TransformBox(At(0, 0, 0, 1), move);
  Check(Near(moved.min[0], 99) && Near(moved.max[0], 101), "translated");
  const Matrix turn{0, 0, 1, 0, 0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 0, 1};
  Box turned = TransformBox(Box{{0, 0, 0}, {4, 1, 1}}, turn);
  Check(Near(turned.min[2], -4) && Near(turned.max[2], 0) && Near(turned.max[0], 1),
        "rotated, from the corners");

  // 1.0f = 0x3F800000 as each fetch swap mode leaves it in memory.
  const uint8_t none[4] = {0x00, 0x00, 0x80, 0x3F};
  const uint8_t s8in16[4] = {0x00, 0x00, 0x3F, 0x80};
  const uint8_t s8in32[4] = {0x3F, 0x80, 0x00, 0x00};
  const uint8_t s16in32[4] = {0x80, 0x3F, 0x00, 0x00};
  Check(FetchFloat(none, 0) == 1.0f, "fetch float, no swap");
  Check(FetchFloat(s8in16, 1) == 1.0f, "fetch float, 8 in 16");
  Check(FetchFloat(s8in32, 2) == 1.0f, "fetch float, 8 in 32");
  Check(FetchFloat(s16in32, 3) == 1.0f, "fetch float, 16 in 32");

  const char* fixed =
      " out float4\toPos_0 : POSITION,\n\tFFP_Transform(worldviewproj_matrix, iPos_0, oPos_0);\n"
      "\tFFP_PixelFog_Depth(worldviewproj_matrix, iPos_0, oTexcoord1_2);\n";
  Check(PlacesVertexLikeFixedPipeline(fixed), "the RTSS fixed transform");
  const char* skinned =
      " out float4\toPos_0 : POSITION,\n\tFFP_Transform(inverse_world_matrix, lLocalParam_0, iPos_0);\n"
      "\tFFP_Transform(viewproj_matrix, lLocalParam_0, oPos_0);\n";
  Check(PlacesVertexLikeFixedPipeline(skinned), "the RTSS hardware skinning");
  const char* moved_input =
      " out float4\toPos_0 : POSITION,\n\tWave(time, iPos_0);\n"
      "\tFFP_Transform(worldviewproj_matrix, iPos_0, oPos_0);\n";
  Check(!PlacesVertexLikeFixedPipeline(moved_input), "input position written first");
  const char* moved_output =
      " out float4\toPos_0 : POSITION,\n\tFFP_Transform(worldviewproj_matrix, iPos_0, oPos_0);\n"
      "\tWave(time, oPos_0);\n";
  Check(!PlacesVertexLikeFixedPipeline(moved_output), "output position written again");
  Check(!PlacesVertexLikeFixedPipeline("float4 main() { return mul(m, p); }"), "anything else");

  BucketBoxes boxes;
  boxes.Store(0x1000, At(0, 0, 0, 1));
  Check(boxes.Find(0x1000) && !boxes.Find(0x2000), "found by bucket");
  boxes.Forget(0x1000);
  Check(!boxes.Find(0x1000), "forgotten when the bucket is destroyed");

  if (failures) return 1;
  std::puts("bucket_cull_test: ok");
  return 0;
}
