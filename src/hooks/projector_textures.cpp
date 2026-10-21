#include "hooks/projector_textures.h"

#include <format>

namespace torchlight::hooks {

std::optional<std::string> ProjectorControl::Check(uint32_t target, const ProjectorTextureFacts& f) {
  const bool chosen = IsProjectorTexture(f), data = LooksLikeProjectorTexture(f);
  if (chosen && !data && !chosen_without_data_) {
    chosen_without_data_ = true;
    return std::format("render texture {:08X} chosen by its listener but {}x{} with {} listeners",
                       target, f.width, f.height, f.listener_count);
  }
  if (!chosen && data && !data_without_choice_) {
    data_without_choice_ = true;
    return std::format("render texture {:08X} is 512x512 with one listener but not the game "
                       "client's",
                       target);
  }
  if (chosen && f.listener_count == 1) {
    if (listener_ == 0) {
      listener_ = f.first_listener;
    } else if (f.first_listener != listener_ && !listeners_differ_) {
      listeners_differ_ = true;
      return std::format("render texture {:08X}'s listener {:08X} is not the other's ({:08X})",
                         target, f.first_listener, listener_);
    }
  }
  return std::nullopt;
}

}  // namespace torchlight::hooks
