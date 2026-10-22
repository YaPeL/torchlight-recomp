#include "frontend/depth_discard.h"

namespace torchlight::frontend {

using namespace torchlight::commands;

std::vector<size_t> PlanDepthDiscards(const std::vector<Command>& commands,
                                      std::optional<SetViewport> viewport,
                                      std::optional<TargetSize> bound) {
  std::vector<size_t> out;
  // A clear with the scissor on covers only its rectangle (D3D9's Clear), so it is never whole.
  // Off when a list starts: OGRE 1.7 turns it on only around light passes and off after them.
  bool scissor = false;
  for (size_t i = 0; i < commands.size(); ++i) {
    if (const auto* v = std::get_if<SetViewport>(&commands[i].payload)) {
      viewport = *v;
      continue;
    }
    if (const auto* sc = std::get_if<SetScissor>(&commands[i].payload)) {
      scissor = sc->enabled;
      continue;
    }
    const auto* target = std::get_if<SetRenderTarget>(&commands[i].payload);
    if (!target) continue;
    const std::optional<TargetSize> left = bound;
    bound = TargetSize{target->width, target->height};
    if (!left || target->width < left->width || target->height < left->height) continue;
    // The pass this bind starts, until something draws, binds another target or presents.
    std::optional<SetViewport> pass_viewport = viewport;
    bool pass_scissor = scissor;
    for (size_t j = i + 1; j < commands.size(); ++j) {
      const auto& payload = commands[j].payload;
      if (const auto* v = std::get_if<SetViewport>(&payload)) {
        pass_viewport = *v;
      } else if (const auto* sc = std::get_if<SetScissor>(&payload)) {
        pass_scissor = sc->enabled;
      } else if (const auto* c = std::get_if<Clear>(&payload)) {
        const bool whole = !pass_scissor && pass_viewport &&
                           pass_viewport->target_guest_address == target->guest_address &&
                           pass_viewport->left == 0 && pass_viewport->top == 0 &&
                           uint32_t(pass_viewport->width) >= target->width &&
                           uint32_t(pass_viewport->height) >= target->height;
        if ((c->buffers & 2) && whole) {
          out.push_back(i);
          break;
        }
      } else if (std::holds_alternative<Draw>(payload) ||
                 std::holds_alternative<SetRenderTarget>(payload) ||
                 std::holds_alternative<Present>(payload)) {
        break;
      }
    }
  }
  return out;
}

}  // namespace torchlight::frontend
