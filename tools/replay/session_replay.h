// replay --session: a live session recording (live/session_file.h) fed frame by frame through the
// live mode's own per-frame step (live/frame_step.h), so the backend accumulates the same state it
// did in the session.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "backend/backend_api.h"

namespace torchlight::replay {

struct SessionOptions {
  std::string path, data_root, out_dir;
  // The frame to report on (render.png, report.txt, --draws / --region / --dump_targets apply to
  // it); default: the last one.
  std::optional<uint64_t> frame;
  // Frames whose output is written as frame_<index>.png.
  std::optional<std::pair<uint64_t, uint64_t>> png_frames;
  uint64_t png_step = 1;
  std::vector<std::pair<size_t, size_t>> draw_ranges;
  std::optional<std::array<uint32_t, 4>> region;
  bool dump_targets = false;
  tl_render_system render_system = TL_RENDER_SYSTEM_GL3PLUS;
  std::string gpu;  // platform GPU id (--gpu); empty: automatic
};

int ReplaySession(const SessionOptions& options);

}  // namespace torchlight::replay
