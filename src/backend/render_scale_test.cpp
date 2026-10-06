// The internal render scale (tl_backend_set_render_scale) and the guest's frame size
// (tl_backend_set_guest_size) on an offscreen backend: targets keep the guest's sizes for viewports
// and reads at any scale, and both can change between frames. Needs a GL context (a display);
// skipped without one.

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>

#include "backend/backend_api.h"
#include "platform/platform.h"

namespace {

constexpr uint32_t kWidth = 1280, kHeight = 720;
constexpr uint64_t kTarget = 7;
constexpr uint32_t kTargetWidth = 64, kTargetHeight = 32;

int failures = 0;

void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

const float kRed[4] = {1, 0, 0, 1}, kBlue[4] = {0, 0, 1, 1};

// One frame on the render target: all red, then blue in the right half's guest viewport.
void DrawFrame(tl_backend* b) {
  tl_backend_begin(b);
  tl_backend_set_target(b, kTarget);
  tl_backend_clear(b, 1, kRed, 1, 0);
  tl_backend_set_viewport(b, kTargetWidth / 2, 0, kTargetWidth / 2, kTargetHeight);
  tl_backend_clear(b, 1, kBlue, 1, 0);
  tl_backend_set_target(b, 0);
  tl_backend_end(b);
}

// Reads the target at its guest size and checks the halves (a column of margin around the edge,
// where filtering down from a larger host size blends the two colours).
void CheckHalves(tl_backend* b, const char* when) {
  std::vector<uint8_t> rgba(kTargetWidth * kTargetHeight * 4);
  if (tl_backend_read_target_rgba(b, kTarget, kTargetWidth, kTargetHeight, rgba.data(),
                                  kTargetWidth * 4) != 0) {
    std::fprintf(stderr, "FAIL: read target (%s)\n", when);
    ++failures;
    return;
  }
  bool ok = true;
  for (uint32_t y = 0; y < kTargetHeight; ++y) {
    for (uint32_t x = 0; x < kTargetWidth; ++x) {
      if (x == kTargetWidth / 2 - 1 || x == kTargetWidth / 2) continue;
      const uint8_t* p = &rgba[(y * kTargetWidth + x) * 4];
      const bool blue = x > kTargetWidth / 2;
      ok = ok && (blue ? p[0] < 8 && p[2] > 247 : p[0] > 247 && p[2] < 8);
    }
  }
  std::fprintf(stderr, "%s: halves %s\n", when, ok ? "ok" : "wrong");
  Check(ok, when);
}

}  // namespace

int main() {
  if (!torchlight::platform::HasDisplay()) {
    std::printf("render scale test: skipped (no display)\n");
    return 77;
  }
  char error[512] = {};
  tl_backend* b = tl_backend_create(TL_RENDER_SYSTEM_GL3PLUS, nullptr, kWidth, kHeight, nullptr, nullptr,
                                    error, sizeof(error));
  if (!b) {
    std::fprintf(stderr, "backend: %s\n", error);
    return 1;
  }
  Check(tl_backend_render_target(b, kTarget, kTargetWidth, kTargetHeight) == 0, "render target");
  DrawFrame(b);
  CheckHalves(b, "scale 1");

  // Between frames, with the target already in use.
  Check(tl_backend_set_render_scale(b, 2) == 0, "scale 2 accepted");
  DrawFrame(b);
  CheckHalves(b, "scale 2");
  Check(tl_backend_set_render_scale(b, 1.5f) == 0, "scale 1.5 accepted");
  DrawFrame(b);
  CheckHalves(b, "scale 1.5");

  Check(tl_backend_set_render_scale(b, 0.1f) != 0, "scale below range refused");
  Check(tl_backend_set_render_scale(b, 8) != 0, "scale above range refused");
  std::vector<uint8_t> wrong(16 * 16 * 4);
  Check(tl_backend_read_target_rgba(b, kTarget, 16, 16, wrong.data(), 16 * 4) != 0,
        "reading at a size other than the guest's is refused");
  std::vector<uint8_t> output(kWidth * kHeight * 4);
  Check(tl_backend_read_rgba(b, output.data(), kWidth * 4) == 0, "output read at the guest size");

  // The guest switches to its 4:3 mode (960x720) at scale 1.5: the main target and the output
  // follow, reads return the new size and the render targets are untouched.
  constexpr uint32_t kNarrow = 960;
  Check(tl_backend_set_guest_size(b, kNarrow, kHeight) == 0, "guest size 960x720 accepted");
  Check(tl_backend_set_guest_size(b, kNarrow, kHeight) == 0, "same guest size is a no-op");
  Check(tl_backend_set_guest_size(b, 0, kHeight) != 0, "empty guest size refused");
  tl_backend_begin(b);
  tl_backend_set_target(b, 0);
  tl_backend_set_viewport(b, 0, 0, kNarrow, kHeight);
  tl_backend_clear(b, 1, kRed, 1, 0);
  tl_backend_end(b);
  std::vector<uint8_t> narrow(kNarrow * kHeight * 4, 0);
  Check(tl_backend_read_rgba(b, narrow.data(), kNarrow * 4) == 0, "output read at 960x720");
  bool red = true;
  for (uint32_t y = 0; y < kHeight; y += 37) {
    for (uint32_t x = 0; x < kNarrow; x += 41) {
      const uint8_t* p = &narrow[(y * kNarrow + x) * 4];
      red = red && p[0] > 247 && p[2] < 8;
    }
  }
  Check(red, "the whole 960x720 output is the cleared colour");
  DrawFrame(b);
  CheckHalves(b, "after the guest size change");

  // The front end's scene clip (tl_backend_set_scene_clip) on a 1680x720 frame: a clear leaves
  // the centred 1280-pixel strip in its colour and the sides black; off, the whole frame.
  {
    constexpr uint32_t kWide = 1680, kStrip = 1280, kSide = (kWide - kStrip) / 2;
    Check(tl_backend_set_guest_size(b, kWide, kHeight) == 0, "guest size 1680x720 accepted");
    auto clear_and_read = [&](int clip) {
      tl_backend_set_scene_clip(b, clip);
      tl_backend_begin(b);
      tl_backend_set_target(b, 0);
      tl_backend_set_viewport(b, 0, 0, kWide, kHeight);
      tl_backend_clear(b, 3, kRed, 1, 0);
      tl_backend_end(b);
      std::vector<uint8_t> rgba(kWide * kHeight * 4, 0);
      tl_backend_read_rgba(b, rgba.data(), kWide * 4);
      return rgba;
    };
    auto red_at = [&](const std::vector<uint8_t>& rgba, uint32_t x, uint32_t y) {
      const uint8_t* p = &rgba[(y * kWide + x) * 4];
      return p[0] > 247 && p[1] < 8 && p[2] < 8;
    };
    auto black_at = [&](const std::vector<uint8_t>& rgba, uint32_t x, uint32_t y) {
      const uint8_t* p = &rgba[(y * kWide + x) * 4];
      return p[0] < 8 && p[1] < 8 && p[2] < 8;
    };
    const auto clipped = clear_and_read(1);
    bool ok = true;
    for (uint32_t y = 2; y < kHeight; y += 97) {
      ok = ok && black_at(clipped, 2, y) && black_at(clipped, kSide - 3, y);
      ok = ok && red_at(clipped, kSide + 3, y) && red_at(clipped, kWide / 2, y);
      ok = ok && red_at(clipped, kSide + kStrip - 3, y) && black_at(clipped, kSide + kStrip + 3, y);
      ok = ok && black_at(clipped, kWide - 3, y);
    }
    Check(ok, "scene clip: centred 16:9 strip in the clear colour, the sides black");
    const auto full = clear_and_read(0);
    Check(red_at(full, 2, 300) && red_at(full, kWide - 3, 300), "no scene clip: whole frame");
  }

  tl_backend_destroy(b);
  if (failures) return 1;
  std::printf("render scale test: ok\n");
  return 0;
}
