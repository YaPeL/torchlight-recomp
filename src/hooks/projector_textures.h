// The game client's shadow and light projector textures updated every other frame
// (--projector_textures_every_other_frame, off by default): a quality option for slow devices. Both
// are render textures OGRE updates every frame (RenderSystem::_updateAllRenderTargets); on the
// POCO X7 they are about a third of the guest thread in a fight (docs/performance-profile.md).
// Skipping an update leaves the texture as the last frame drew it (the backend keeps a render
// target's content, backend/render_scale_test.cpp), so the shadows and the light map lag a frame.
//
// Chosen by their listener, the game client (guest_abi kGameClientRenderTargetListenerVtable), not
// by name; the data the 2026-10-21 run found for them (512x512, one listener, the same in both) is
// only a control, logged once when it disagrees.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace torchlight::hooks {

// What the hook reads of a render texture.
struct ProjectorTextureFacts {
  uint32_t width = 0, height = 0;
  uint32_t listener_count = 0;
  bool game_client_listener = false;  // one of its listeners has the game client's vtable
  uint32_t first_listener = 0;
};

// Whether the target is one of the projector textures.
constexpr bool IsProjectorTexture(const ProjectorTextureFacts& f) { return f.game_client_listener; }

// The control: the data alone (512x512, exactly one listener). Agrees with the choice when both say
// the same.
constexpr bool LooksLikeProjectorTexture(const ProjectorTextureFacts& f) {
  return f.width == 512 && f.height == 512 && f.listener_count == 1;
}

// Per target: whether this update runs. The first update of a target runs, then every other one.
class EveryOtherFrame {
 public:
  bool Run(uint32_t target) { return (count_[target]++ & 1) == 0; }
  void Forget(uint32_t target) { count_.erase(target); }

 private:
  std::unordered_map<uint32_t, uint32_t> count_;
};

// The control's notes, each logged once: a chosen target without the data, a target with the data
// not chosen, chosen targets with different listeners. Returns the text of a new note, else none.
class ProjectorControl {
 public:
  std::optional<std::string> Check(uint32_t target, const ProjectorTextureFacts& f);

 private:
  bool chosen_without_data_ = false, data_without_choice_ = false, listeners_differ_ = false;
  uint32_t listener_ = 0;
};

}  // namespace torchlight::hooks
