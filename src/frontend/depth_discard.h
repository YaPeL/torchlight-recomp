// Which target binds of a frame may first discard the depth of the target they leave
// (tl_backend_discard_depth). The guest binds one depth surface for all its targets
// (guest_abi/ogre_layout.h, render_system) and every pass starts with its viewport's clear: when
// the pass a bind starts clears the depth over the whole of its target before it draws, and that
// target is at least as large as the one left, everything the left target's depth held in the
// guest's surface is overwritten, so the host may drop it. No command or backend types beyond the
// command list.

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "commands/types.h"

namespace torchlight::frontend {

struct TargetSize {
  uint32_t width = 0, height = 0;
};

// Indices of the SetRenderTarget commands of `commands` whose bind may discard the depth of the
// target bound before it. `viewport` is the viewport set before the list (the guest's
// _setViewport binds the viewport's target from inside, so a bind is recorded after the
// SetViewport that caused it); `bound` the size of the target bound before the list, when known
// (nothing is discarded at the list's first bind without it).
std::vector<size_t> PlanDepthDiscards(const std::vector<commands::Command>& commands,
                                      std::optional<commands::SetViewport> viewport,
                                      std::optional<TargetSize> bound, bool scissor = false);

}  // namespace torchlight::frontend
