#include "achievements/achievement_list.h"

#include <atomic>
#include <string>

#include <imgui.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/overlay/achievements_overlay.h>

#include "achievements/list_model.h"
#include "achievements/runtime.h"
#include "achievements/ui_strings.h"

namespace torchlight::achievements {
namespace {

std::atomic<bool> g_open{false};

// B on the pad or Escape closes the list, as the game's own menus.
bool CloseRequested() {
  return ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
         ImGui::IsKeyPressed(ImGuiKey_Escape, false);
}

void CloseButton(bool& close) {
  ImGui::Spacing();
  if (ImGui::Button(Translate("Close").c_str())) close = true;
}

// Xbox set: the SDK's own list (names, descriptions, gamerscore and icons from the XEX), plus a way
// to close it.
class XboxList : public rex::ui::AchievementsOverlayDialog {
 public:
  using AchievementsOverlayDialog::AchievementsOverlayDialog;

 protected:
  void OnDraw(ImGuiIO& io) override {
    AchievementsOverlayDialog::OnDraw(io);
    bool close = CloseRequested();
    if (ImGui::Begin("Achievements##overlay")) CloseButton(close);  // appends to the SDK's window
    ImGui::End();
    if (close) Close();
  }
  void OnClose() override { g_open = false; }
};

// PC set: our texts, unlocked state, counter progress, and the ones this version cannot award.
class PcList : public rex::ui::ImGuiDialog {
 public:
  explicit PcList(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    const auto state = SnapshotState();
    const auto rows = BuildList(state.value_or(State{}));
    int unlocked = 0, available = 0;
    for (const auto& r : rows) {
      if (r.availability == Availability::kAvailable) ++available;
      if (r.unlocked) ++unlocked;
    }
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, 40.0f), ImGuiCond_FirstUseEver,
                            ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(640.0f, 560.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.92f);
    bool close = CloseRequested();
    const std::string title = Translate("Achievements (PC, incomplete)") + "##pc_achievements";
    if (ImGui::Begin(title.c_str(), nullptr, ImGuiWindowFlags_NoCollapse)) {
      ImGui::Text("%s: %d / %d", Translate("Unlocked").c_str(), unlocked, available);
      ImGui::ProgressBar(available ? float(unlocked) / available : 0.0f, ImVec2(-1.0f, 6.0f), "");
      ImGui::Separator();
      ImGui::BeginChild("##pc_list", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing() * 1.5f));
      for (const auto& r : rows) {
        const bool earnable = r.availability == Availability::kAvailable;
        const ImVec4 color = r.unlocked ? ImVec4(1.0f, 0.85f, 0.4f, 1.0f)
                             : earnable ? ImVec4(0.9f, 0.9f, 0.9f, 1.0f)
                                        : ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
        ImGui::TextColored(color, "%s %s", r.unlocked ? "[*]" : "[ ]",
                           Translate(r.english).c_str());
        if (!earnable) {
          const char* why = "Not available in this version";  // mods (list_model.h)
          ImGui::TextDisabled("    %s", Translate(why).c_str());
        } else if (r.counter && !r.unlocked) {
          ImGui::TextDisabled("    %d / %d", r.value, r.threshold);
        }
      }
      ImGui::EndChild();
      CloseButton(close);
    }
    ImGui::End();
    if (close) Close();
  }
  void OnClose() override { g_open = false; }
};

}  // namespace

void OpenAchievementList(rex::Runtime* runtime, rex::ui::ImmediateDrawer* immediate_drawer) {
  rex::ui::ImGuiDrawer* drawer = runtime ? runtime->imgui_drawer() : nullptr;
  if (!drawer || g_open.exchange(true)) return;
  if (NativeEnabled()) {
    new PcList(drawer);
  } else if (runtime->kernel_state()) {
    new XboxList(drawer, immediate_drawer, runtime, &runtime->kernel_state()->achievements());
  } else {
    g_open = false;
    return;
  }
  REXLOG_INFO("achievements: list opened ({})", NativeEnabled() ? "PC" : "Xbox 360");
}
}  // namespace torchlight::achievements
