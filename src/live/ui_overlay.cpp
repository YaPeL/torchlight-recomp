#include "live/ui_overlay.h"

#include <mutex>
#include <utility>

namespace torchlight::live {

struct UiOverlay::Mailbox {
  std::mutex mutex;
  uint64_t next_texture_id = 1;
  std::vector<UiTextureOp> ops;
  std::optional<UiFrame> frame;

  void Push(UiTextureOp op) {
    std::lock_guard lock(mutex);
    ops.push_back(std::move(op));
  }
};

class UiOverlay::Texture : public rex::ui::ImmediateTexture {
 public:
  Texture(uint32_t width, uint32_t height, uint64_t id, std::shared_ptr<Mailbox> mailbox)
      : ImmediateTexture(width, height), id_(id), mailbox_(std::move(mailbox)) {}
  ~Texture() override {
    UiTextureOp op;
    op.id = id_;
    op.release = true;
    mailbox_->Push(std::move(op));
  }
  uint64_t id() const { return id_; }

 private:
  uint64_t id_;
  std::shared_ptr<Mailbox> mailbox_;
};

UiOverlay::UiOverlay() : mailbox_(std::make_shared<Mailbox>()) {}
UiOverlay::~UiOverlay() = default;

std::unique_ptr<rex::ui::ImmediateTexture> UiOverlay::CreateTexture(
    uint32_t width, uint32_t height, rex::ui::ImmediateTextureFilter filter, bool is_repeated,
    const uint8_t* data) {
  UiTextureOp op;
  {
    std::lock_guard lock(mailbox_->mutex);
    op.id = mailbox_->next_texture_id++;
  }
  op.width = width;
  op.height = height;
  op.linear = filter == rex::ui::ImmediateTextureFilter::kLinear;
  op.repeat = is_repeated;
  if (data) op.rgba.assign(data, data + size_t(width) * height * 4);
  uint64_t id = op.id;
  mailbox_->Push(std::move(op));
  return std::make_unique<Texture>(width, height, id, mailbox_);
}

void UiOverlay::Begin(rex::ui::UIDrawContext& ui_draw_context, float coordinate_space_width,
                      float coordinate_space_height) {
  ImmediateDrawer::Begin(ui_draw_context, coordinate_space_width, coordinate_space_height);
  building_ = UiFrame();
  building_.space_width = ImmediateDrawer::coordinate_space_width();
  building_.space_height = ImmediateDrawer::coordinate_space_height();
}

void UiOverlay::BeginDrawBatch(const rex::ui::ImmediateDrawBatch& batch) {
  batch_ = &batch;
  batch_vertex_base_ = uint32_t(building_.vertices.size());
  batch_index_base_ = uint32_t(building_.indices.size());
  for (int i = 0; i < batch.vertex_count; ++i) {
    const rex::ui::ImmediateVertex& v = batch.vertices[i];
    building_.vertices.push_back({v.x, v.y, v.u, v.v, v.color});
  }
  if (batch.indices) {
    building_.indices.insert(building_.indices.end(), batch.indices,
                             batch.indices + batch.index_count);
  }
}

void UiOverlay::Draw(const rex::ui::ImmediateDraw& draw) {
  if (!batch_ || draw.count <= 0) return;
  tl_ui_cmd cmd = {};
  if (draw.texture) cmd.texture = static_cast<Texture*>(draw.texture)->id();
  cmd.lines = draw.primitive_type == rex::ui::ImmediatePrimitiveType::kLines;
  cmd.count = uint32_t(draw.count);
  cmd.base_vertex = int32_t(batch_vertex_base_) + draw.base_vertex;
  if (batch_->indices) {
    cmd.index_start = batch_index_base_ + uint32_t(draw.index_offset);
  } else {
    // Vertex ranges become indices, so every command of the frame is indexed.
    cmd.index_start = uint32_t(building_.indices.size());
    for (int i = 0; i < draw.count; ++i) building_.indices.push_back(uint16_t(i));
  }
  cmd.scissor = draw.scissor;
  cmd.scissor_left = draw.scissor_left;
  cmd.scissor_top = draw.scissor_top;
  cmd.scissor_right = draw.scissor_right;
  cmd.scissor_bottom = draw.scissor_bottom;
  building_.cmds.push_back(cmd);
}

void UiOverlay::EndDrawBatch() { batch_ = nullptr; }

void UiOverlay::End() {
  {
    std::lock_guard lock(mailbox_->mutex);
    mailbox_->frame = std::move(building_);
  }
  building_ = UiFrame();
  ImmediateDrawer::End();
}

void UiOverlay::PublishEmpty() {
  std::lock_guard lock(mailbox_->mutex);
  mailbox_->frame = UiFrame();
}

void UiOverlay::Take(std::vector<UiTextureOp>& ops, std::optional<UiFrame>& frame) {
  std::lock_guard lock(mailbox_->mutex);
  for (auto& op : mailbox_->ops) ops.push_back(std::move(op));
  mailbox_->ops.clear();
  frame = std::move(mailbox_->frame);
  mailbox_->frame.reset();
}

}  // namespace torchlight::live
