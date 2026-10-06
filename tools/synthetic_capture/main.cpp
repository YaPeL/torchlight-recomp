// synthetic_capture: writes a .tlcap with nothing of the game, for checking a packaged replay
// (packaging/linux/check_appimage.sh, CI's package-check): one frame that clears the main target
// to a known colour, and a reference image of that colour. The replay loads OGRE's plugins and
// media, renders and reads back like the game's captures; matching the reference shows the whole
// path works.
//
// Usage: synthetic_capture OUTPUT.tlcap

#include <cstdio>
#include <string>

#include "commands/serialize.h"
#include "commands/types.h"

using namespace torchlight::commands;

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: %s OUTPUT.tlcap\n", argv[0]);
    return 2;
  }
  constexpr uint32_t kWidth = 320, kHeight = 180;
  // Exactly representable in 8 bits.
  constexpr uint8_t kRed = 51, kGreen = 102, kBlue = 204;
  Capture c;
  c.meta.first_swap = 1;
  c.meta.frame_count = 1;
  c.meta.created_utc = "2026-10-05T00:00:00Z";
  c.meta.notes = "synthetic: one cleared frame, nothing of the game";
  Frame frame;
  frame.first_swap = 1;
  auto add = [&](CommandPayload payload) { frame.commands.push_back({0, std::move(payload)}); };
  add(BeginFrame{});
  add(SetViewport{0x1000, 0, 0, 0, int32_t(kWidth), int32_t(kHeight)});
  add(Clear{7, {kRed / 255.0f, kGreen / 255.0f, kBlue / 255.0f, 1.0f}, 1.0f, 0});
  add(EndFrame{});
  add(Present{1});
  c.frames.push_back(std::move(frame));
  ReferenceImage reference;
  reference.width = kWidth;
  reference.height = kHeight;
  reference.stride = kWidth * 4;
  reference.rgbx.resize(size_t(reference.stride) * kHeight);
  for (size_t i = 0; i < reference.rgbx.size(); i += 4) {
    reference.rgbx[i] = kRed, reference.rgbx[i + 1] = kGreen, reference.rgbx[i + 2] = kBlue;
    reference.rgbx[i + 3] = 255;
  }
  c.reference = std::move(reference);
  std::string error;
  if (!WriteCaptureFile(argv[1], c, error)) {
    std::fprintf(stderr, "synthetic_capture: %s\n", error.c_str());
    return 1;
  }
  return 0;
}
