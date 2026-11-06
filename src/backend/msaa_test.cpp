// Multisampling of the main target (tl_backend_set_msaa): a tilted white triangle on black, drawn
// on the main target and read from the output after the display gamma (identity ramp). Without
// multisampling every pixel is black or white; with it the edges get the values between and
// nothing away from them changes (the resolve before the display gamma, on GL by a blit or in tile
// memory, on Direct3D 11 into its own texture: ogre-d3d11-resolve-multisample-blit.patch); turned
// off again the image is the first one, exactly. The coverage probe counts pixels, not samples:
// checked at 4x, the count every driver keeps (Mesa's llvmpipe runs 2x as 4 samples while OGRE
// reports the 2 asked, which only this diagnostic would notice). With the render system of its command line (test_render_system.h). Needs a display;
// skipped without one.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "backend/backend_api.h"
#include "backend/test_render_system.h"
#include "platform/platform.h"

namespace {

constexpr uint32_t kWidth = 256, kHeight = 144;
constexpr uint64_t kTriangle = 31;

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

// One frame: the triangle on black, the coverage probe on the whole frame; the output read back.
std::vector<uint8_t> Frame(tl_backend* b, uint32_t* probe) {
  static const float kBlack[4] = {0, 0, 0, 1};
  tl_backend_begin(b);
  tl_backend_set_target(b, 0);
  tl_backend_set_viewport(b, 0, 0, kWidth, kHeight);
  tl_backend_clear(b, 1, kBlack, 1, 0);
  tl_backend_set_probe(b, 1, 0, 0, kWidth, kHeight);
  SetState(b);
  static const tl_vertex_element element = {0, 0, 0, kFloat3, kPosition};
  const uint64_t streams[1] = {kTriangle};
  const uint32_t strides[1] = {12};
  tl_draw d = {};
  d.primitive = kTriangleStrip;
  d.elements = &element;
  d.element_count = 1;
  d.stream_buffers = streams;
  d.stream_strides = strides;
  d.stream_count = 1;
  d.vertex_count = 3;
  for (int i = 0; i < 4; ++i) d.guest_wvp[i * 5] = 1;
  d.colour_source = TL_COLOUR_VERTEX;
  Check(tl_backend_draw(b, &d) == TL_DRAWN, "triangle drawn");
  *probe = tl_backend_probe_samples(b);
  tl_backend_set_probe(b, 0, 0, 0, 0, 0);
  tl_backend_end(b);
  std::vector<uint8_t> rgba(kWidth * kHeight * 4);
  Check(tl_backend_read_rgba(b, rgba.data(), kWidth * 4) == 0, "output read back");
  return rgba;
}

uint8_t Grey(const std::vector<uint8_t>& rgba, uint32_t x, uint32_t y) {
  return rgba[(y * kWidth + x) * 4];
}

// Pixels neither black nor white.
uint32_t Between(const std::vector<uint8_t>& rgba) {
  uint32_t n = 0;
  for (uint32_t y = 0; y < kHeight; ++y) {
    for (uint32_t x = 0; x < kWidth; ++x) {
      const uint8_t g = Grey(rgba, x, y);
      if (g > 8 && g < 247) ++n;
    }
  }
  return n;
}

// Pixels that differ from the image without multisampling where that image has no edge within a
// pixel (all its 3x3 neighbourhood black, or all white).
uint32_t ChangedAwayFromEdges(const std::vector<uint8_t>& off, const std::vector<uint8_t>& msaa) {
  uint32_t n = 0;
  for (uint32_t y = 1; y + 1 < kHeight; ++y) {
    for (uint32_t x = 1; x + 1 < kWidth; ++x) {
      if (std::abs(int(Grey(off, x, y)) - int(Grey(msaa, x, y))) <= 4) continue;
      bool edge = false;
      for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
          if (Grey(off, x + dx, y + dy) != Grey(off, x, y)) edge = true;
        }
      }
      if (!edge) ++n;
    }
  }
  return n;
}

}  // namespace

int main(int argc, char** argv) {
  torchlight::backend_test::RenderSystemArgs args;
  if (!torchlight::backend_test::ParseRenderSystemArgs(argc, argv, args)) return 2;
  if (!torchlight::platform::HasDisplay()) {
    std::printf("msaa test: skipped (no display)\n");
    return 77;
  }
  char error[512] = {};
  tl_backend* b = tl_backend_create(args.render_system, args.gpu.c_str(), kWidth, kHeight, nullptr,
                                    nullptr, error, sizeof(error));
  if (!b) {
    std::fprintf(stderr, "backend: %s\n", error);
    return 1;
  }
  // A triangle with no horizontal or vertical edge, in clip space.
  const float v[9] = {-0.8f, -0.7f, 0.5f, 0.7f, -0.45f, 0.5f, -0.15f, 0.8f, 0.5f};
  Check(tl_backend_vertex_buffer(b, kTriangle, v, sizeof(v), 0) == 0, "vertex buffer");

  uint32_t probe_off = 0, probe_msaa = 0, probe_again = 0;
  const std::vector<uint8_t> off = Frame(b, &probe_off);
  const uint32_t max = tl_backend_max_msaa(b);
  std::fprintf(stderr, "without multisampling: %u pixels between black and white, probe %u; "
               "device maximum %ux\n", Between(off), probe_off, max);
  Check(Between(off) == 0, "without multisampling the edges are hard");
  Check(probe_off > 0, "the probe counts the triangle");

  if (max < 2) {
    std::printf("msaa test (%s): no multisampling on this device; only the off path checked\n",
                args.name);
  } else {
    const uint32_t applied = tl_backend_set_msaa(b, 2);
    Check(applied >= 2, "2x applied");
    const std::vector<uint8_t> msaa = Frame(b, &probe_msaa);
    const uint32_t between = Between(msaa), away = ChangedAwayFromEdges(off, msaa);
    std::fprintf(stderr, "%ux: %u pixels between black and white, %u changed away from the "
                 "edges, probe %u\n", applied, between, away, probe_msaa);
    Check(between > 0, "multisampled edges get the values between");
    Check(away == 0, "nothing away from the edges changes");
    if (max >= 4 && tl_backend_set_msaa(b, 4) == 4) {
      uint32_t probe4 = 0;
      Frame(b, &probe4);
      std::fprintf(stderr, "4x: probe %u\n", probe4);
      // The probe reports pixels: within 3 % of the count without multisampling.
      Check(probe4 * 100 >= probe_off * 97 && probe4 * 100 <= probe_off * 103,
            "the probe counts pixels under multisampling");
    }

    Check(tl_backend_set_msaa(b, 0) == 0, "multisampling turned off");
    const std::vector<uint8_t> again = Frame(b, &probe_again);
    Check(again == off, "turned off, the image is the first one");
    Check(probe_again == probe_off, "turned off, the probe count is the first one");
  }

  tl_backend_destroy(b);
  if (failures) return 1;
  std::printf("msaa test (%s): ok\n", args.name);
  return 0;
}
