#include "live/touch_overlay.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <vector>

#include <imgui.h>
#include <rex/logging.h>
#include <rex/ui/image_decode.h>
#include <rex/ui/immediate_drawer.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>

#include "input/pad_state.h"
#include "live/touch_input.h"
#include "platform/platform.h"

namespace torchlight::live {
namespace {

std::atomic<rex::ui::ImGuiDialog*> g_dialog{nullptr};

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

// A PNG of data/ui/touch/ as a texture the drawer can draw; null when it cannot be read.
std::unique_ptr<rex::ui::ImmediateTexture> LoadIcon(rex::ui::ImmediateDrawer* textures, const char* name) {
  if (!textures) return nullptr;
  const std::filesystem::path path = std::filesystem::path(platform::ResourceDir()) / "data" / "ui" / "touch" / name;
  std::ifstream in(path, std::ios::binary);
  const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  int w = 0, h = 0;
  std::vector<uint8_t> rgba = bytes.empty() ? std::vector<uint8_t>{} : rex::ui::DecodeImageRGBA(bytes.data(), bytes.size(), w, h);
  if (rgba.empty()) {
    REXLOG_WARN("touch: no icon {} (plain shapes instead)", path.string());
    return nullptr;
  }
  return textures->CreateTexture(uint32_t(w), uint32_t(h), rex::ui::ImmediateTextureFilter::kLinear, false,
                                 rgba.data());
}

ImTextureID Id(const std::unique_ptr<rex::ui::ImmediateTexture>& t) {
  return reinterpret_cast<ImTextureID>(t.get());
}

class OverlayDialog : public rex::ui::ImGuiDialog {
 public:
  OverlayDialog(rex::ui::ImGuiDrawer* drawer, rex::ui::ImmediateDrawer* textures)
      : ImGuiDialog(drawer),
        button_(LoadIcon(textures, "button_circle.png")),
        pressed_(LoadIcon(textures, "button_circle_highlight.png")),
        pad_(LoadIcon(textures, "joystick_circle_pad_a.png")),
        nub_(LoadIcon(textures, "joystick_circle_nub_a.png")) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    TouchSnapshot snap;
    if (!ReadTouchControls(snap) || !snap.visible || snap.width <= 0) return;
    // The layout is in the window's pixels; ImGui's frame spans the window in its own units.
    const float k = io.DisplaySize.x / snap.width;
    ImDrawList* list = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    for (const input::TouchButton& b : snap.layout.buttons) {
      const ImVec2 c(b.x * k, b.y * k);
      const float r = b.radius * k;
      const bool down = Pressed(b, snap.state);
      if (button_) {
        const ImVec2 a(c.x - r, c.y - r), z(c.x + r, c.y + r);
        list->AddImage(Id(button_), a, z, ImVec2(0, 0), ImVec2(1, 1), Colour(1, 1, 1, kOpacity));
        if (down && pressed_) list->AddImage(Id(pressed_), a, z, ImVec2(0, 0), ImVec2(1, 1), Colour(1, 1, 1, kOpacity));
      } else {
        list->AddCircleFilled(c, r, down ? Colour(1, 1, 1, 0.45f * kOpacity) : Colour(0, 0, 0, 0.45f * kOpacity), 48);
        list->AddCircle(c, r, Colour(1, 1, 1, 0.85f * kOpacity), 48, std::max(2.0f, r * 0.06f));
      }
      float lr, lg, lb;
      LabelColour(b, lr, lg, lb);
      const std::string label(b.name);
      const float size = r * (label.size() > 2 ? 0.55f : 0.9f);
      const ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.0f, label.c_str());
      // The names stronger than the buttons, to read them at a glance.
      list->AddText(font, size, ImVec2(c.x - extent.x / 2, c.y - extent.y / 2),
                    Colour(lr, lg, lb, std::min(1.0f, kOpacity + 0.3f)), label.c_str());
    }
    // The stick: its base where it rests or where the thumb landed, the knob where the thumb is.
    const float sr = snap.layout.stick_radius * k;
    const ImVec2 base(snap.stick_base_x * k, snap.stick_base_y * k);
    const ImVec2 knob(base.x + snap.state.left_x / 32767.0f * sr, base.y - snap.state.left_y / 32767.0f * sr);
    // Fainter while it rests, so the resting place is a hint, not a control in the way.
    const float a = (snap.stick_held ? 1.0f : 0.6f) * kOpacity;
    if (pad_ && nub_) {
      list->AddImage(Id(pad_), ImVec2(base.x - sr, base.y - sr), ImVec2(base.x + sr, base.y + sr), ImVec2(0, 0),
                     ImVec2(1, 1), Colour(1, 1, 1, a));
      const float nr = sr * 0.5f;
      list->AddImage(Id(nub_), ImVec2(knob.x - nr, knob.y - nr), ImVec2(knob.x + nr, knob.y + nr), ImVec2(0, 0),
                     ImVec2(1, 1), Colour(1, 1, 1, a));
    } else {
      list->AddCircleFilled(base, sr, Colour(0, 0, 0, 0.35f * a), 64);
      list->AddCircle(base, sr, Colour(1, 1, 1, 0.85f * a), 64, std::max(2.0f, sr * 0.04f));
      list->AddCircleFilled(knob, sr * 0.42f, Colour(1, 1, 1, 0.6f * a), 48);
    }
  }

 private:
  std::unique_ptr<rex::ui::ImmediateTexture> button_, pressed_, pad_, nub_;
};

}  // namespace

rex::ui::ImGuiDialog* TouchOverlayDialog(rex::ui::ImGuiDrawer* drawer, rex::ui::ImmediateDrawer* textures) {
  if (rex::ui::ImGuiDialog* dialog = g_dialog.load()) return dialog;
  TouchSnapshot snap;
  if (!drawer || !ReadTouchControls(snap)) return nullptr;  // no touch controls
  g_dialog = new OverlayDialog(drawer, textures);
  REXLOG_INFO("touch: controls drawn over the game");
  return g_dialog.load();
}

bool TouchControlsShown() {
  TouchSnapshot snap;
  return ReadTouchControls(snap) && snap.visible;
}

void ForgetTouchOverlayDialog() {
  // Deleted here, in the drawer or not: a dialog takes itself out of its drawer when deleted, so the
  // drawer does not delete it again.
  delete g_dialog.exchange(nullptr);
}

}  // namespace torchlight::live
