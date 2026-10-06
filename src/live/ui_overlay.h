// Host UI overlay: the runtime's ImGui dialogs (XamShowKeyboardUI, XamShowMessageBoxUI) in only
// mode, where the GPU plugin has no presenter to draw them. An ImmediateDrawer that records what
// ImGuiDrawer::Draw emits into UI frames instead of drawing; the live consumer hands the latest one
// to the backend (tl_backend_set_ui_frame), which draws it over the window. Not part of the
// guest's command stream or of captures.
//
// Threads: the ImmediateDrawer calls come from the UI thread (ImGuiDrawer has no locks and runs
// there); Take comes from the consumer. A texture operation is never lost; of the frames, the
// latest wins.

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <rex/ui/immediate_drawer.h>

#include "backend/backend_api.h"

namespace torchlight::live {

struct UiFrame {
  float space_width = 0, space_height = 0;  // the coordinate space spanning the window
  std::vector<tl_ui_vertex> vertices;
  std::vector<uint16_t> indices;  // every command is indexed
  std::vector<tl_ui_cmd> cmds;
};

struct UiTextureOp {
  uint64_t id = 0;
  bool release = false;
  uint32_t width = 0, height = 0;
  bool linear = true, repeat = false;
  std::vector<uint8_t> rgba;  // RGBA8, top row first
};

class UiOverlay : public rex::ui::ImmediateDrawer {
 public:
  UiOverlay();
  ~UiOverlay() override;

  std::unique_ptr<rex::ui::ImmediateTexture> CreateTexture(uint32_t width, uint32_t height,
                                                           rex::ui::ImmediateTextureFilter filter,
                                                           bool is_repeated,
                                                           const uint8_t* data) override;
  void Begin(rex::ui::UIDrawContext& ui_draw_context, float coordinate_space_width,
             float coordinate_space_height) override;
  void BeginDrawBatch(const rex::ui::ImmediateDrawBatch& batch) override;
  void Draw(const rex::ui::ImmediateDraw& draw) override;
  void EndDrawBatch() override;
  void End() override;

  // No dialogs: publishes an empty frame, so the last dialog does not stay on screen.
  void PublishEmpty();

  // Consumer: appends the texture operations since the last call, in order, and moves out the
  // latest frame published since then (left empty when there is none).
  void Take(std::vector<UiTextureOp>& ops, std::optional<UiFrame>& frame);

 private:
  struct Mailbox;
  class Texture;

  std::shared_ptr<Mailbox> mailbox_;  // shared with the textures, which may outlive a frame
  UiFrame building_;
  const rex::ui::ImmediateDrawBatch* batch_ = nullptr;
  uint32_t batch_vertex_base_ = 0, batch_index_base_ = 0;
};

}  // namespace torchlight::live
