// A stage's constants across render target binds (BindChangedParameters): the backend skips
// binding constants that did not change since the stage last received them, and binding a render
// target must not leave a draw without them. Direct3D 11's _setRenderTarget calls ClearState,
// which unbinds the constant buffers; a draw with the same constants after a target switch then
// read zeros (its world-view-projection and its alpha test function, CMPF_ALWAYS_FAIL) and drew
// nothing. Two quads with the same program and constants, one on each half of a render target,
// with a draw on another target of the same size in between: both halves and the other target
// must be covered. The state is set before every draw, as the frontend does (backend_api.h:
// Direct3D 11's target bind resets the device's state too). With the render system of its command line (test_render_system.h). Needs a
// display; skipped without one.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "backend/backend_api.h"
#include "backend/test_render_system.h"
#include "platform/platform.h"

namespace {

constexpr uint32_t kWidth = 1280, kHeight = 720;
constexpr uint64_t kTarget = 7, kOther = 8;
constexpr uint64_t kLeftQuad = 21, kRightQuad = 22;
constexpr uint32_t kTargetWidth = 64, kTargetHeight = 32;

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

// The same program and constants for every draw: the vertex colour (white without one), no
// texture, the identity as the world-view-projection, an alpha test that always passes.
void SetState(tl_backend* b);

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

void BindCleared(tl_backend* b, uint64_t target) {
  static const float kBlack[4] = {0, 0, 0, 1};
  tl_backend_set_target(b, target);
  tl_backend_set_viewport(b, 0, 0, kTargetWidth, kTargetHeight);
  tl_backend_clear(b, 1, kBlack, 1, 0);
}

// Whether every pixel of the columns [x0, x1) of a target is white.
bool White(tl_backend* b, uint64_t target, uint32_t x0, uint32_t x1) {
  std::vector<uint8_t> rgba(kTargetWidth * kTargetHeight * 4);
  if (tl_backend_read_target_rgba(b, target, kTargetWidth, kTargetHeight, rgba.data(),
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
    std::printf("draw constants test: skipped (no display)\n");
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
  Check(tl_backend_render_target(b, kOther, kTargetWidth, kTargetHeight) == 0, "other target");
  const auto left = Quad(-1, 0), right = Quad(0, 1);
  Check(tl_backend_vertex_buffer(b, kLeftQuad, left.data(), uint32_t(left.size()), 0) == 0,
        "left quad");
  Check(tl_backend_vertex_buffer(b, kRightQuad, right.data(), uint32_t(right.size()), 0) == 0,
        "right quad");

  for (int frame = 0; frame < 2; ++frame) {
    tl_backend_begin(b);
    BindCleared(b, kOther);
    BindCleared(b, kTarget);
    Check(DrawQuad(b, kLeftQuad) == TL_DRAWN, "left quad drawn");
    // Another target of the same size: the same constants again, after a target bind.
    tl_backend_set_target(b, kOther);
    tl_backend_set_viewport(b, 0, 0, kTargetWidth, kTargetHeight);
    Check(DrawQuad(b, kLeftQuad) == TL_DRAWN, "quad on the other target drawn");
    // Back to the first target: the same constants once more.
    tl_backend_set_target(b, kTarget);
    tl_backend_set_viewport(b, 0, 0, kTargetWidth, kTargetHeight);
    Check(DrawQuad(b, kRightQuad) == TL_DRAWN, "right quad drawn");
    tl_backend_set_target(b, 0);
    tl_backend_end(b);

    // A column of margin at the edge between the halves.
    const bool left_ok = White(b, kTarget, 0, kTargetWidth / 2 - 1);
    const bool right_ok = White(b, kTarget, kTargetWidth / 2 + 1, kTargetWidth);
    const bool other_ok = White(b, kOther, 0, kTargetWidth / 2 - 1);
    std::fprintf(stderr, "frame %d: left %s, right %s, other target %s\n", frame,
                 left_ok ? "ok" : "missing", right_ok ? "ok" : "missing",
                 other_ok ? "ok" : "missing");
    Check(left_ok, "the first quad covers the left half");
    Check(other_ok, "the quad after a target switch covers the other target");
    Check(right_ok, "the quad after switching back covers the right half");
  }

  tl_backend_destroy(b);
  if (failures) return 1;
  std::printf("draw constants test (%s): ok\n", args.name);
  return 0;
}
