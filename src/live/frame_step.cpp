#include "live/frame_step.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

namespace torchlight::live {

FrameStep RunFrameStep(LiveFrame& frame, LiveContentSource& content, frontend::Frontend& frontend,
                       const std::function<void()>& after_commands) {
  FrameStep step;
  using Clock = std::chrono::steady_clock;
  auto lap = Clock::now();
  auto since = [&lap] {
    auto now = Clock::now();
    double ms = std::chrono::duration<double, std::milli>(now - lap).count();
    lap = now;
    return ms;
  };
  auto changes = content.Apply(frame);
  for (const auto& id : changes.destroyed_textures) frontend.ReleaseTexture(id);
  for (const auto& id : changes.destroyed_buffers) frontend.ReleaseBufferOwner(id);
  auto textures_start = std::chrono::steady_clock::now();
  for (const auto& t : changes.textures) frontend.PrepareTexture(t);
  step.textures_created = changes.textures.size();
  if (!changes.textures.empty()) {
    step.texture_ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - textures_start)
                          .count();
  }
  step.content_ms = since();
  const double draws_before = frontend.stats().draw_ms;
  frontend.BeginFrame();
  for (const auto& cmd : frame.commands) {
    step.commands_by_opcode[commands::OpcodeOf(cmd.payload) - 1]++;
    frontend.Execute(cmd);
  }
  step.commands_ms = since();
  step.draws_ms = frontend.stats().draw_ms - draws_before;
  frontend.EndFrame();
  step.end_ms = since();
  if (after_commands) after_commands();
  step.after_ms = since();
  for (auto key : content.Finish()) frontend.ReleaseContent(key);
  step.release_ms = since();
  return step;
}

void AddCommandCounts(CommandCounts& counts, const FrameStep& step) {
  for (size_t i = 0; i < counts.size(); ++i) counts[i] += step.commands_by_opcode[i];
}

std::string FormatCommandCounts(const CommandCounts& counts, uint64_t frames, size_t top) {
  double per = double(std::max<uint64_t>(frames, 1));
  uint64_t total = 0;
  std::vector<size_t> order;
  for (size_t i = 0; i < counts.size(); ++i) {
    total += counts[i];
    if (counts[i]) order.push_back(i);
  }
  std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return counts[a] > counts[b]; });
  char line[96];
  std::snprintf(line, sizeof(line), "%.1f per frame", total / per);
  std::string out = line;
  for (size_t k = 0; k < order.size() && k < top; ++k) {
    std::snprintf(line, sizeof(line), "\n    %8.1f  %s", counts[order[k]] / per,
                  commands::kOpcodeNames[order[k]]);
    out += line;
  }
  return out;
}

}  // namespace torchlight::live
