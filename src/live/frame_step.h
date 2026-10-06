// The live mode's work on one consumed frame, shared by the live mode's backend thread and the
// session replay (replay --session), so a recorded session goes through exactly the path the
// backend took.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "frontend/frontend.h"
#include "live/frame_queue.h"
#include "live/live_content_source.h"

namespace torchlight::live {

struct FrameStep {
  size_t textures_created = 0;
  double texture_ms = 0;  // creating this frame's textures
  // Where the frame's time went (ms): resources applied and textures prepared, commands run
  // through the frontend (backend draws included, also given apart), the frame's end (display
  // gamma), after_commands (presentation) and freeing superseded content.
  double content_ms = 0, commands_ms = 0, draws_ms = 0, end_ms = 0, after_ms = 0, release_ms = 0;
  std::array<uint32_t, commands::kOpcodeCount> commands_by_opcode{};  // index: opcode - 1
};

// Applies the frame's resources (textures are created here, between frames, never inside a draw),
// runs its commands through the frontend, calls `after_commands` (presentation) and then frees
// superseded and destroyed content on the host.
FrameStep RunFrameStep(LiveFrame& frame, LiveContentSource& content, frontend::Frontend& frontend,
                       const std::function<void()>& after_commands);

// Commands consumed per frame by type, accumulated over `frames` frames (summaries): the total and
// the most frequent types.
using CommandCounts = std::array<uint64_t, commands::kOpcodeCount>;
void AddCommandCounts(CommandCounts& counts, const FrameStep& step);
std::string FormatCommandCounts(const CommandCounts& counts, uint64_t frames, size_t top = 10);

}  // namespace torchlight::live
