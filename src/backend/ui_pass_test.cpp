// The host UI pass (tl_backend_set_ui_frame) on an offscreen backend: a synthetic frame over the
// output, checked pixel by pixel, with the render system of its command line
// (test_render_system.h). Needs a display; skipped without one.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>

#include "backend/backend_api.h"
#include "backend/test_render_system.h"
#include "platform/platform.h"

namespace {

constexpr uint32_t kWidth = 1280, kHeight = 720;
// Coordinate space 100 x 50: 12.8 x 14.4 pixels per unit.
constexpr float kSpaceWidth = 100, kSpaceHeight = 50;

int failures = 0;

void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

uint32_t Rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  return uint32_t(r) | uint32_t(g) << 8 | uint32_t(b) << 16 | uint32_t(a) << 24;
}

// A quad (two triangles) from (x0, y0) to (x1, y1) in coordinate space units.
void Quad(std::vector<tl_ui_vertex>& v, std::vector<uint16_t>& i, float x0, float y0, float x1,
          float y1, uint32_t colour) {
  uint16_t base = uint16_t(v.size());
  v.push_back({x0, y0, 0, 0, colour});
  v.push_back({x1, y0, 1, 0, colour});
  v.push_back({x1, y1, 1, 1, colour});
  v.push_back({x0, y1, 0, 1, colour});
  for (uint16_t k : {0, 1, 2, 0, 2, 3}) i.push_back(base + k);
}

// Pixel at the centre of coordinate space point (x, y), top row first.
const uint8_t* At(const std::vector<uint8_t>& image, float x, float y) {
  uint32_t px = uint32_t(x * kWidth / kSpaceWidth), py = uint32_t(y * kHeight / kSpaceHeight);
  return &image[(size_t(py) * kWidth + px) * 4];
}

bool Near(const uint8_t* p, int r, int g, int b) {
  return std::abs(p[0] - r) <= 2 && std::abs(p[1] - g) <= 2 && std::abs(p[2] - b) <= 2;
}

}  // namespace

int main(int argc, char** argv) {
  torchlight::backend_test::RenderSystemArgs args;
  if (!torchlight::backend_test::ParseRenderSystemArgs(argc, argv, args)) return 2;
  if (!torchlight::platform::HasDisplay()) {
    std::printf("ui pass test: skipped (no display)\n");
    return 77;
  }
  char error[512] = {};
  tl_backend* b = tl_backend_create(args.render_system, args.gpu.c_str(), kWidth, kHeight, nullptr,
                                    nullptr, error, sizeof(error));
  if (!b) {
    std::fprintf(stderr, "backend: %s\n", error);
    return 1;
  }
  // 2 x 1 texture: green, blue.
  const uint8_t texels[8] = {0, 255, 0, 255, 0, 0, 255, 255};
  Check(tl_backend_ui_texture(b, 7, 2, 1, 0, 0, texels) == 0, "texture created");

  std::vector<tl_ui_vertex> v;
  std::vector<uint16_t> i;
  std::vector<tl_ui_cmd> cmds;
  auto cmd = [&](uint64_t texture, size_t first_index) {
    tl_ui_cmd c = {};
    c.texture = texture;
    c.index_start = uint32_t(first_index);
    c.count = uint32_t(i.size() - first_index);
    cmds.push_back(c);
    return &cmds.back();
  };
  // Red, near the top left: checks the orientation (y down from the top).
  size_t first = i.size();
  Quad(v, i, 10, 5, 30, 15, Rgba(255, 0, 0, 255));
  cmd(0, first);
  // Textured with point filtering: left half green, right half blue.
  first = i.size();
  Quad(v, i, 50, 5, 70, 15, Rgba(255, 255, 255, 255));
  cmd(7, first);
  // White across the bottom, scissored to its left part.
  first = i.size();
  Quad(v, i, 10, 30, 90, 45, Rgba(255, 255, 255, 255));
  tl_ui_cmd* clipped = cmd(0, first);
  clipped->scissor = 1;
  clipped->scissor_left = 10;
  clipped->scissor_top = 30;
  clipped->scissor_right = 20;
  clipped->scissor_bottom = 45;
  // Half-transparent white: blended over the black clear.
  first = i.size();
  Quad(v, i, 80, 5, 95, 15, Rgba(255, 255, 255, 128));
  cmd(0, first);
  tl_backend_set_ui_frame(b, kSpaceWidth, kSpaceHeight, v.data(), uint32_t(v.size()), i.data(),
                          uint32_t(i.size()), cmds.data(), uint32_t(cmds.size()));

  const float black[4] = {0, 0, 0, 1};
  tl_backend_begin(b);
  tl_backend_clear(b, 1, black, 1, 0);
  tl_backend_end(b);
  std::vector<uint8_t> image(size_t(kWidth) * kHeight * 4);
  Check(tl_backend_read_rgba(b, image.data(), kWidth * 4) == 0, "read back");

  Check(Near(At(image, 20, 10), 255, 0, 0), "untextured quad at its place");
  Check(Near(At(image, 20, 22), 0, 0, 0), "nothing below the top quad");
  Check(Near(At(image, 55, 10), 0, 255, 0), "texture: left texel");
  Check(Near(At(image, 65, 10), 0, 0, 255), "texture: right texel");
  Check(Near(At(image, 15, 37), 255, 255, 255), "scissor: inside");
  Check(Near(At(image, 50, 37), 0, 0, 0), "scissor: outside");
  Check(Near(At(image, 87, 10), 128, 128, 128), "alpha blending");

  // No commands: no UI.
  tl_backend_set_ui_frame(b, kSpaceWidth, kSpaceHeight, nullptr, 0, nullptr, 0, nullptr, 0);
  tl_backend_begin(b);
  tl_backend_clear(b, 1, black, 1, 0);
  tl_backend_end(b);
  Check(tl_backend_read_rgba(b, image.data(), kWidth * 4) == 0, "read back");
  Check(Near(At(image, 20, 10), 0, 0, 0), "empty frame clears the UI");

  tl_backend_ui_release_texture(b, 7);
  tl_backend_destroy(b);
  if (failures) return 1;
  std::printf("ui pass test (%s): ok\n", args.name);
  return 0;
}
