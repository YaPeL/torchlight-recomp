// Draws that share one vertex layout with different vertex buffers: tl_backend_draw keeps one
// Ogre::VertexData per layout and rebinds its streams from draw to draw, and forces the vertex
// array object's update only where the render system has them (RSC_VAO; backend.cpp,
// VertexLayout). Without that update (Direct3D 11) the streams must still follow each draw's
// buffers. Quads on the quarters of a target, in a different order each frame over three frames,
// each from its own buffer with the same layout: every quarter must be covered, and the layout
// created once. (The case the forced update exists for, a new buffer at a freed one's address,
// depends on the allocator and is not reproduced here.) With the render system of its command line
// (test_render_system.h). Needs a display; skipped without one.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "backend/backend_api.h"
#include "backend/test_render_system.h"
#include "platform/platform.h"

namespace {

constexpr uint32_t kWidth = 1280, kHeight = 720;
constexpr uint64_t kTarget = 7;
constexpr uint32_t kTargetWidth = 64, kTargetHeight = 32;
// Four buffers, one per quarter of the target, all with the same layout.
constexpr uint64_t kQuarterBuffers[4] = {31, 32, 33, 34};

// commands/types.h values the draw uses.
constexpr uint8_t kTriangleStrip = 4, kFloat3 = 2, kPosition = 0;
constexpr uint8_t kBlendOne = 0, kBlendZero = 1, kCompareAlways = 1, kCullNone = 0, kSolid = 2;

int failures = 0;

void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

// A quad from clip x0 to x1, the full height, as a triangle strip of float3 positions (little
// endian, no fetch swap).
std::vector<uint8_t> Quad(float x0, float x1) {
  const float v[12] = {x0, 1, 0.5f, x1, 1, 0.5f, x0, -1, 0.5f, x1, -1, 0.5f};
  std::vector<uint8_t> bytes(sizeof(v));
  std::memcpy(bytes.data(), v, sizeof(v));
  return bytes;
}

void SetState(tl_backend* b) {
  tl_state s = {};
  s.blend_src = kBlendOne;
  s.blend_dst = kBlendZero;
  s.depth_func_requested = s.depth_func_effective = kCompareAlways;
  s.cull_mode = kCullNone;
  s.alpha_func = kCompareAlways;
  s.colour_write = 0xF;
  s.polygon_mode = kSolid;
  tl_backend_set_state(b, &s);
}

int DrawQuad(tl_backend* b, uint64_t buffer) {
  SetState(b);
  static const tl_vertex_element element = {0, 0, 0, kFloat3, kPosition};
  const uint64_t streams[1] = {buffer};
  const uint32_t strides[1] = {12};
  tl_draw d = {};
  d.primitive = kTriangleStrip;
  d.elements = &element;
  d.element_count = 1;
  d.stream_buffers = streams;
  d.stream_strides = strides;
  d.stream_count = 1;
  d.vertex_count = 4;
  for (int i = 0; i < 4; ++i) d.guest_wvp[i * 5] = 1;
  d.colour_source = TL_COLOUR_VERTEX;
  return tl_backend_draw(b, &d);
}

// Whether every pixel of the columns [x0, x1) of the target is white.
bool White(tl_backend* b, uint32_t x0, uint32_t x1) {
  std::vector<uint8_t> rgba(kTargetWidth * kTargetHeight * 4);
  if (tl_backend_read_target_rgba(b, kTarget, kTargetWidth, kTargetHeight, rgba.data(),
                                  kTargetWidth * 4) != 0) {
    return false;
  }
  for (uint32_t y = 0; y < kTargetHeight; ++y) {
    for (uint32_t x = x0; x < x1; ++x) {
      const uint8_t* p = &rgba[(y * kTargetWidth + x) * 4];
      if (p[0] < 248 || p[1] < 248 || p[2] < 248) return false;
    }
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  torchlight::backend_test::RenderSystemArgs args;
  if (!torchlight::backend_test::ParseRenderSystemArgs(argc, argv, args)) return 2;
  if (!torchlight::platform::HasDisplay()) {
    std::printf("vertex rebind test: skipped (no display)\n");
    return 77;
  }
  char error[512] = {};
  tl_backend* b = tl_backend_create(args.render_system, args.gpu.c_str(), kWidth, kHeight, nullptr,
                                    nullptr, error, sizeof(error));
  if (!b) {
    std::fprintf(stderr, "backend: %s\n", error);
    return 1;
  }
  Check(tl_backend_render_target(b, kTarget, kTargetWidth, kTargetHeight) == 0, "render target");
  for (int q = 0; q < 4; ++q) {
    const auto quad = Quad(-1 + q * 0.5f, -0.5f + q * 0.5f);
    Check(tl_backend_vertex_buffer(b, kQuarterBuffers[q], quad.data(), uint32_t(quad.size()), 0) ==
              0,
          "quarter buffer");
  }
  tl_backend_counters counters = {};
  tl_backend_take_counters(b, &counters);

  // Each frame draws the quarters in another order, so every draw rebinds the shared layout's
  // stream to a buffer the previous draw did not use.
  static const int kOrders[3][4] = {{0, 1, 2, 3}, {3, 1, 0, 2}, {2, 0, 3, 1}};
  static const float kBlack[4] = {0, 0, 0, 1};
  for (int frame = 0; frame < 3; ++frame) {
    tl_backend_begin(b);
    tl_backend_set_target(b, kTarget);
    tl_backend_set_viewport(b, 0, 0, kTargetWidth, kTargetHeight);
    tl_backend_clear(b, 1, kBlack, 1, 0);
    for (int q : kOrders[frame]) Check(DrawQuad(b, kQuarterBuffers[q]) == TL_DRAWN, "quad drawn");
    tl_backend_set_target(b, 0);
    tl_backend_end(b);

    // A column of margin at each edge between the quarters.
    const uint32_t quarter = kTargetWidth / 4;
    bool all = true;
    for (uint32_t q = 0; q < 4; ++q) {
      const bool ok = White(b, q * quarter + 1, (q + 1) * quarter - 1);
      std::fprintf(stderr, "frame %d: quarter %u %s\n", frame, q, ok ? "ok" : "missing");
      all = all && ok;
    }
    Check(all, "every quad covers its quarter (its own buffer, not the previous draw's)");
  }
  tl_backend_take_counters(b, &counters);
  std::fprintf(stderr, "vertex layouts created: %u\n", counters.vertex_layouts_created);
  Check(counters.vertex_layouts_created == 1, "the four buffers share one vertex layout");

  tl_backend_destroy(b);
  if (failures) return 1;
  std::printf("vertex rebind test (%s): ok\n", args.name);
  return 0;
}
