#include "wizard/wizard_ui.h"

#include <algorithm>

#include "imgui_internal.h"

namespace torchlight::wizard {

void Ui::Go(int page) {
  history_.push_back(page_);
  page_ = page;
  entered_ = true;
  entered_frames_ = 0;
}

void Ui::Back() {
  if (history_.empty()) return;
  page_ = history_.back();
  history_.pop_back();
  entered_ = true;
  entered_frames_ = 0;
}

void Ui::Reset(int page) {
  history_.clear();
  page_ = page;
  entered_ = true;
  entered_frames_ = 0;
}

namespace {

// The height of a window's content shown at once (a bordered child has padding).
float ViewHeight(const ImGuiWindow& window) {
  return window.InnerRect.GetHeight() - window.WindowPadding.y * 2;
}

}  // namespace

void Ui::Frame(bool can_go_back, const std::function<void()>& draw) {
  want_focus_ = false;  // a request no item took never reaches a later page
  notice_window_ = nullptr;
  const ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(io.DisplaySize);
  ImGui::Begin("##wizard", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
  const int before = page_;
  // Not on a page's first frames: the press that brought it there is still down.
  if (can_go_back && !entered_ &&
      (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
       ImGui::IsKeyPressed(ImGuiKey_Escape, false))) {
    Back();
  }
  // Each page its own ids, so its body's scroll starts at the top the first time it is shown.
  ImGui::PushID(page_);
  draw();
  ImGui::PopID();
  ImGui::End();
  in_view_ = true;
  if (notice_window_) {
    const ImGuiContext& g = *ImGui::GetCurrentContext();
    const auto* window = static_cast<const ImGuiWindow*>(notice_window_);
    const float top = window->Scroll.y, bottom = top + ViewHeight(*window);
    in_view_ = notice_top_ >= top - 0.5f && notice_top_ < bottom;
    if (g.NavWindow == window) {
      const ImRect& focused = window->NavRectRel[ImGuiNavLayer_Main];
      in_view_ = in_view_ && focused.Min.y >= top - 0.5f && focused.Max.y <= bottom + 0.5f;
    }
  }
  // The focus is asked for on a page's first two frames (ImGui takes it from the second).
  if (page_ == before && entered_ && ++entered_frames_ >= 2) entered_ = false;
}

void Ui::Heading(const std::string& title, const std::string& step) {
  ImGui::PushFont(fonts_.bold, 30.0f);
  ImGui::TextUnformatted(title_.c_str());
  ImGui::PopFont();
  if (!step.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextUnformatted(step.c_str());
    ImGui::PopStyleColor();
  }
  ImGui::Separator();
  ImGui::Spacing();
  ImGui::PushFont(fonts_.bold, 24.0f);
  ImGui::TextUnformatted(title.c_str());
  ImGui::PopFont();
  ImGui::Spacing();
}

float Ui::BodyHeight() const {
  const ImGuiStyle& style = ImGui::GetStyle();
  return ImGui::GetContentRegionAvail().y -
         (ImGui::GetFrameHeight() + style.ItemSpacing.y * 3 + ImGui::GetTextLineHeight());
}

bool Ui::Footer(const char* next_label, bool next_enabled, bool back_enabled, bool focus_next) {
  ImGui::Separator();
  if (!back_enabled) ImGui::BeginDisabled();
  if (ImGui::Button(texts.back.c_str(), ImVec2(ImGui::GetFontSize() * 7, 0))) Back();
  if (!back_enabled) ImGui::EndDisabled();
  bool chosen = false;
  if (next_label) {
    const float width = std::max(ImGui::GetFontSize() * 7, ImGui::CalcTextSize(next_label).x +
                                                               ImGui::GetStyle().FramePadding.x * 2);
    ImGui::SameLine(ImGui::GetWindowWidth() - width - ImGui::GetStyle().WindowPadding.x);
    if (!next_enabled) ImGui::BeginDisabled();
    if (focus_next && entered_ && next_enabled) FocusNext();
    chosen = ImGui::Button(next_label, ImVec2(width, 0));
    Took();
    if (!next_enabled) ImGui::EndDisabled();
    // Start: the next step from anywhere on the page.
    if (next_enabled && !entered_ && ImGui::IsKeyPressed(ImGuiKey_GamepadStart, false)) {
      chosen = true;
    }
  }
  const std::string& hint = next_label ? texts.hint_next : texts.hint;
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize(hint.c_str()).x) * 0.5f);
  ImGui::TextUnformatted(hint.c_str());
  ImGui::PopStyleColor();
  return chosen;
}

void Ui::Wrapped(const std::string& text) { ImGui::TextWrapped("%s", text.c_str()); }

void Ui::Muted(const std::string& text) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  Wrapped(text);
  ImGui::PopStyleColor();
}

void Ui::Notice() {
  if (notice_window_) return;
  ImGuiWindow* window = ImGui::GetCurrentContext()->CurrentWindow;
  notice_window_ = window;
  notice_top_ = ImGui::GetCursorScreenPos().y - window->DC.CursorStartPos.y;
}

void Ui::Error(const std::string& text) {
  Notice();
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 0.45f, 0.40f, 1.00f));
  Wrapped(text);
  ImGui::PopStyleColor();
}

void Ui::Warning(const char* id, const std::string& title, const std::string& text) {
  Notice();
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.30f, 0.20f, 0.00f, 0.55f));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.90f, 0.70f, 0.00f, 1.00f));
  ImGui::BeginChild(id, ImVec2(0, 0),
                    ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY |
                        ImGuiChildFlags_AlwaysUseWindowPadding);
  ImGui::PushFont(fonts_.bold, 0.0f);
  ImGui::TextColored(ImVec4(1.00f, 0.80f, 0.30f, 1.00f), "%s", title.c_str());
  ImGui::PopFont();
  Wrapped(text);
  ImGui::EndChild();
  ImGui::PopStyleColor(2);
}

void Ui::Took() {
  if (!want_focus_) return;
  want_focus_ = false;
  // Through the context, not ImGui::GetCurrentWindow: that inline reads the global the runtime's
  // library holds, which a Windows executable cannot reach without an import.
  ImGui::SetFocusID(ImGui::GetItemID(), ImGui::GetCurrentContext()->CurrentWindow);
  ImGui::SetNavCursorVisible(true);
  // In the window's content: the least scroll that shows the item, from the top.
  ImGuiWindow* window = ImGui::GetCurrentContext()->CurrentWindow;
  const float item_bottom = ImGui::GetItemRectMax().y - window->DC.CursorStartPos.y;
  float scroll =
      std::max(0.0f, item_bottom + ImGui::GetStyle().ItemSpacing.y - ViewHeight(*window));
  // A notice above it stays in view, from its top, even when that leaves the item below.
  if (notice_window_ == window) scroll = std::min(scroll, notice_top_);
  ImGui::SetScrollY(scroll);
}

}  // namespace torchlight::wizard
