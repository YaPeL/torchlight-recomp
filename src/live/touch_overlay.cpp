#include "live/touch_overlay.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>

#include <imgui.h>
#include <rex/logging.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>

#include "input/pad_state.h"
#include "live/touch_input.h"

namespace torchlight::live {
namespace {

std::atomic<rex::ui::ImGuiDialog*> g_dialog{nullptr};
std::atomic<bool> g_other_dialog{false};

// The controls' opacity (A4.5 makes it a setting).
constexpr float kOpacity = 0.6f;

ImU32 Colour(float r, float g, float b, float a) {
  return IM_COL32(int(r * 255), int(g * 255), int(b * 255), int(std::clamp(a, 0.0f, 1.0f) * 255));
}

// The 360's face button colours; the rest are white.
void LabelColour(const input::TouchButton& b, float& r, float& g, float& bl) {
  r = g = bl = 1.0f;
  if (b.buttons & input::pad::kA) { r = 0.42f; g = 0.78f; bl = 0.18f; }
  if (b.buttons & input::pad::kB) { r = 0.90f; g = 0.22f; bl = 0.20f; }
  if (b.buttons & input::pad::kX) { r = 0.25f; g = 0.50f; bl = 0.95f; }
  if (b.buttons & input::pad::kY) { r = 1.00f; g = 0.80f; bl = 0.15f; }
}

bool Pressed(const input::TouchButton& b, const input::PadState& s) {
  return (b.buttons && (s.buttons & b.buttons) == b.buttons) || (b.left_trigger && s.left_trigger) ||
         (b.right_trigger && s.right_trigger);
}

class OverlayDialog : public rex::ui::ImGuiDialog {
 public:
  explicit OverlayDialog(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    TouchSnapshot snap;
    if (g_other_dialog.load() || !ReadTouchControls(snap) || !snap.visible || snap.width <= 0) return;
    // The layout is in the window's pixels; ImGui's frame spans the window in its own units.
    const float k = io.DisplaySize.x / snap.width;
    ImDrawList* list = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    for (const input::TouchButton& b : snap.layout.buttons) {
      const ImVec2 c(b.x * k, b.y * k);
      const float r = b.radius * k;
      const bool down = Pressed(b, snap.state);
      list->AddCircleFilled(c, r, down ? Colour(1, 1, 1, 0.45f * kOpacity) : Colour(0, 0, 0, 0.45f * kOpacity), 48);
      list->AddCircle(c, r, Colour(1, 1, 1, 0.85f * kOpacity), 48, std::max(2.0f, r * 0.06f));
      float lr, lg, lb;
      LabelColour(b, lr, lg, lb);
      const std::string label(b.name);
      const float size = r * (label.size() > 2 ? 0.55f : 0.9f);
      const ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.0f, label.c_str());
      list->AddText(font, size, ImVec2(c.x - extent.x / 2, c.y - extent.y / 2), Colour(lr, lg, lb, kOpacity),
                    label.c_str());
    }
    // The stick: its base where it rests or where the thumb landed, the knob where the thumb is.
    const float sr = snap.layout.stick_radius * k;
    const ImVec2 base(snap.stick_base_x * k, snap.stick_base_y * k);
    list->AddCircleFilled(base, sr, Colour(0, 0, 0, (snap.stick_held ? 0.35f : 0.2f) * kOpacity), 64);
    list->AddCircle(base, sr, Colour(1, 1, 1, (snap.stick_held ? 0.85f : 0.5f) * kOpacity), 64,
                    std::max(2.0f, sr * 0.04f));
    const ImVec2 knob(base.x + snap.state.left_x / 32767.0f * sr, base.y - snap.state.left_y / 32767.0f * sr);
    list->AddCircleFilled(knob, sr * 0.42f, Colour(1, 1, 1, (snap.stick_held ? 0.6f : 0.3f) * kOpacity), 48);
  }
};

}  // namespace

rex::ui::ImGuiDialog* TouchOverlayDialog(rex::ui::ImGuiDrawer* drawer) {
  if (rex::ui::ImGuiDialog* dialog = g_dialog.load()) return dialog;
  TouchSnapshot snap;
  if (!drawer || !ReadTouchControls(snap)) return nullptr;  // no touch controls
  g_dialog = new OverlayDialog(drawer);
  REXLOG_INFO("touch: controls drawn over the game");
  return g_dialog.load();
}

void SetTouchOverlayDialogOpen(bool open) { g_other_dialog = open; }

}  // namespace torchlight::live
