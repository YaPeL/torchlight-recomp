// UiOverlay without a window: ImmediateDrawer calls as ImGuiDrawer::Draw makes them, checked
// against the recorded UI frame and texture operations.

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <vector>

#include <rex/ui/presenter.h>

#include "live/ui_overlay.h"

namespace {

void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    std::exit(1);
  }
}

}  // namespace

int main() {
  using namespace torchlight::live;
  namespace ui = rex::ui;
  UiOverlay overlay;
  std::vector<UiTextureOp> ops;
  std::optional<UiFrame> frame;

  const uint8_t rgba[2 * 2 * 4] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
  auto texture = overlay.CreateTexture(2, 2, ui::ImmediateTextureFilter::kNearest, true, rgba);
  overlay.Take(ops, frame);
  Check(ops.size() == 1 && !ops[0].release && ops[0].id != 0, "texture created");
  Check(ops[0].width == 2 && ops[0].height == 2 && !ops[0].linear && ops[0].repeat,
        "texture attributes");
  Check(ops[0].rgba.size() == 16 && ops[0].rgba[15] == 16, "texture content");
  Check(!frame, "no frame before drawing");
  const uint64_t texture_id = ops[0].id;
  ops.clear();

  // Two batches, as ImGui emits one per draw list; the second is scissored.
  ui::AppUIDrawContext context(1920, 1080);
  overlay.Begin(context, 1280, 720);
  const ui::ImmediateVertex a[3] = {{0, 0, 0, 0, 0xFF0000FF}, {10, 0, 1, 0, 0xFF0000FF},
                                    {0, 10, 0, 1, 0xFF0000FF}};
  const uint16_t a_indices[3] = {0, 1, 2};
  ui::ImmediateDrawBatch batch_a;
  batch_a.vertices = a;
  batch_a.vertex_count = 3;
  batch_a.indices = a_indices;
  batch_a.index_count = 3;
  overlay.BeginDrawBatch(batch_a);
  ui::ImmediateDraw draw_a;
  draw_a.count = 3;
  draw_a.texture = texture.get();
  overlay.Draw(draw_a);
  overlay.EndDrawBatch();
  const ui::ImmediateVertex b[4] = {{20, 20, 0, 0, 0xFFFFFFFF}, {30, 20, 0, 0, 0xFFFFFFFF},
                                    {30, 30, 0, 0, 0xFFFFFFFF}, {20, 30, 0, 0, 0xFFFFFFFF}};
  const uint16_t b_indices[6] = {0, 1, 2, 0, 2, 3};
  ui::ImmediateDrawBatch batch_b;
  batch_b.vertices = b;
  batch_b.vertex_count = 4;
  batch_b.indices = b_indices;
  batch_b.index_count = 6;
  overlay.BeginDrawBatch(batch_b);
  ui::ImmediateDraw draw_b;
  draw_b.count = 3;
  draw_b.index_offset = 3;
  draw_b.scissor = true;
  draw_b.scissor_left = 21;
  draw_b.scissor_top = 22;
  draw_b.scissor_right = 29;
  draw_b.scissor_bottom = 28;
  overlay.Draw(draw_b);
  overlay.EndDrawBatch();
  overlay.End();

  overlay.Take(ops, frame);
  Check(ops.empty(), "no texture operations while drawing");
  Check(frame.has_value(), "frame published at End");
  Check(frame->space_width == 1280 && frame->space_height == 720, "coordinate space");
  Check(frame->vertices.size() == 7 && frame->indices.size() == 9, "batches concatenated");
  Check(frame->vertices[3].x == 20 && frame->vertices[0].colour == 0xFF0000FF, "vertex content");
  Check(frame->cmds.size() == 2, "one command per draw");
  const tl_ui_cmd& c0 = frame->cmds[0];
  Check(c0.texture == texture_id && c0.index_start == 0 && c0.count == 3 && c0.base_vertex == 0 &&
            !c0.scissor && !c0.lines,
        "first command");
  const tl_ui_cmd& c1 = frame->cmds[1];
  Check(c1.texture == 0 && c1.index_start == 3 + 3 && c1.count == 3 && c1.base_vertex == 3,
        "second command: offsets into the concatenated arrays");
  Check(c1.scissor && c1.scissor_left == 21 && c1.scissor_bottom == 28, "scissor");

  // Non-positive coordinate space: the render target's size. A batch without indices: its vertex
  // range becomes indices.
  overlay.Begin(context, 0, 0);
  ui::ImmediateDrawBatch batch_c;
  batch_c.vertices = b;
  batch_c.vertex_count = 4;
  overlay.BeginDrawBatch(batch_c);
  ui::ImmediateDraw draw_c;
  draw_c.primitive_type = ui::ImmediatePrimitiveType::kLines;
  draw_c.count = 2;
  draw_c.base_vertex = 1;
  overlay.Draw(draw_c);
  overlay.EndDrawBatch();
  overlay.End();
  overlay.Take(ops, frame);
  Check(frame && frame->space_width == 1920 && frame->space_height == 1080,
        "render target size as the coordinate space");
  Check(frame->indices.size() == 2 && frame->indices[0] == 0 && frame->indices[1] == 1,
        "vertex range as indices");
  Check(frame->cmds.size() == 1 && frame->cmds[0].lines && frame->cmds[0].base_vertex == 1 &&
            frame->cmds[0].index_start == 0,
        "lines command");

  // Only the latest frame is kept; an empty one clears the UI.
  overlay.Begin(context, 1280, 720);
  overlay.End();
  overlay.PublishEmpty();
  overlay.Take(ops, frame);
  Check(frame && frame->cmds.empty(), "latest frame wins");
  overlay.Take(ops, frame);
  Check(!frame, "a frame is taken once");

  texture.reset();
  overlay.Take(ops, frame);
  Check(ops.size() == 1 && ops[0].release && ops[0].id == texture_id, "texture released");
  std::printf("ui overlay test: ok\n");
  return 0;
}
