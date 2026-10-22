// The present pass (tl_backend_present) in a visible window: it draws the main target through the
// display gamma itself, so the window must show what the output (the main target after the gamma
// pass, tl_backend_read_output_rgba) shows: the same pixels when the window is the output's size,
// the output filtered bilinearly when the internal render scale makes it smaller. With a gamma
// ramp and without one (an identity ramp skips the lookup). With the render system of its command
// line (test_render_system.h). Needs a display; skipped without one.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "backend/backend_api.h"
#include "backend/test_render_system.h"
#include "platform/platform.h"

namespace {

constexpr uint32_t kWidth = 256, kHeight = 128;

int failures = 0;

void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

// A frame with edges in every direction: bands of colours in viewports one to a few pixels wide,
// over a background, so filtering between texels shows.
void DrawFrame(tl_backend* b) {
  tl_backend_begin(b);
  tl_backend_set_target(b, 0);
  const float background[4] = {0.2f, 0.4f, 0.6f, 1};
  tl_backend_clear(b, 1, background, 1, 0);
  for (int32_t i = 0; i < 24; ++i) {
    const float colour[4] = {float(i % 5) / 4, float(i % 7) / 6, float(i % 3) / 2, 1};
    tl_backend_set_viewport(b, i * 10, (i * 7) % 96, 1 + i % 4, 32);
    tl_backend_clear(b, 1, colour, 1, 0);
    tl_backend_set_viewport(b, (i * 13) % 200, i * 5, 48, 1 + i % 3);
    tl_backend_clear(b, 1, colour, 1, 0);
  }
  tl_backend_set_viewport(b, 0, 0, kWidth, kHeight);
  tl_backend_end(b);
}

// A ramp of the guest's table form (commands::SetGammaRamp: 10-bit values in the high bits), a
// different curve per channel; or the identity.
void SetRamp(tl_backend* b, bool identity) {
  uint16_t values[768];
  for (int ch = 0; ch < 3; ++ch) {
    for (int v = 0; v < 256; ++v) {
      const double x = v / 255.0;
      const double y = identity ? x : std::pow(x, 0.5 + 0.25 * ch) * 0.9 + 0.05;
      values[ch * 256 + v] = uint16_t(std::lround(y * 1023)) << 6;
    }
  }
  tl_backend_set_gamma_ramp(b, 0, values);
}

// The output at its host size, filtered bilinearly to the window's size (texel centres, clamped
// at the edges), as a sampler would.
std::vector<uint8_t> Bilinear(const std::vector<uint8_t>& src, uint32_t sw, uint32_t sh,
                              uint32_t dw, uint32_t dh) {
  std::vector<uint8_t> out(size_t(dw) * dh * 4);
  auto at = [&](int x, int y, int c) {
    x = std::clamp(x, 0, int(sw) - 1);
    y = std::clamp(y, 0, int(sh) - 1);
    return float(src[(size_t(y) * sw + x) * 4 + c]);
  };
  for (uint32_t y = 0; y < dh; ++y) {
    for (uint32_t x = 0; x < dw; ++x) {
      const float px = (x + 0.5f) * sw / dw - 0.5f, py = (y + 0.5f) * sh / dh - 0.5f;
      const int x0 = int(std::floor(px)), y0 = int(std::floor(py));
      const float fx = px - x0, fy = py - y0;
      for (int c = 0; c < 4; ++c) {
        const float top = at(x0, y0, c) * (1 - fx) + at(x0 + 1, y0, c) * fx;
        const float bottom = at(x0, y0 + 1, c) * (1 - fx) + at(x0 + 1, y0 + 1, c) * fx;
        out[(size_t(y) * dw + x) * 4 + c] = uint8_t(std::lround(top * (1 - fy) + bottom * fy));
      }
    }
  }
  return out;
}

// One frame presented and read back from the window, compared with the output (filtered to the
// window's size when it is smaller): at most `tolerance` apart on every channel.
void CheckPresent(tl_backend* b, int tolerance, const char* when) {
  DrawFrame(b);
  std::vector<uint8_t> window(kWidth * kHeight * 4);
  int done = 0;
  tl_backend_read_next_present(b, window.data(), kWidth, kHeight, kWidth * 4, &done);
  Check(tl_backend_present(b) != 0, "present");
  if (!done) {
    std::fprintf(stderr, "FAIL: window not read (%s)\n", when);
    ++failures;
    return;
  }
  uint32_t ow = 0, oh = 0;
  tl_backend_output_size(b, &ow, &oh);
  std::vector<uint8_t> output(size_t(ow) * oh * 4);
  Check(tl_backend_read_output_rgba(b, output.data(), ow * 4) == 0, "output read");
  const std::vector<uint8_t> expected =
      ow == kWidth && oh == kHeight ? output : Bilinear(output, ow, oh, kWidth, kHeight);
  int worst = 0;
  for (size_t i = 0; i < window.size(); ++i) {
    if (i % 4 == 3) continue;  // the window's alpha is not shown
    worst = std::max(worst, std::abs(int(window[i]) - int(expected[i])));
  }
  std::fprintf(stderr, "%s: largest difference %d (output %ux%u)\n", when, worst, ow, oh);
  Check(worst <= tolerance, when);
}

}  // namespace

int main(int argc, char** argv) {
  torchlight::backend_test::RenderSystemArgs args;
  if (!torchlight::backend_test::ParseRenderSystemArgs(argc, argv, args)) return 2;
  if (!torchlight::platform::HasDisplay()) {
    std::printf("present test: skipped (no display)\n");
    return 77;
  }
  char error[512] = {};
  tl_backend* b = tl_backend_create(args.render_system, args.gpu.c_str(), kWidth, kHeight, nullptr,
                                    "present test", error, sizeof(error));
  if (!b) {
    std::fprintf(stderr, "backend: %s\n", error);
    return 1;
  }
  tl_backend_set_vsync(b, 0);
  // The window's size is the output's: no filtering, the same pixels.
  SetRamp(b, false);
  CheckPresent(b, 0, "gamma, scale 1");
  SetRamp(b, true);
  CheckPresent(b, 0, "identity, scale 1");
  // Half the size internally: the window filters the output up.
  Check(tl_backend_set_render_scale(b, 0.5f) == 0, "scale 0.5 accepted");
  SetRamp(b, false);
  CheckPresent(b, 1, "gamma, scale 0.5");
  SetRamp(b, true);
  CheckPresent(b, 1, "identity, scale 0.5");
  tl_backend_destroy(b);
  std::printf("present test (%s): %s\n", args.name, failures ? "FAILED" : "ok");
  return failures ? 1 : 0;
}
