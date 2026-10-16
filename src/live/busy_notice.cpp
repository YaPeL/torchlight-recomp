#include "live/busy_notice.h"

#include <algorithm>
#include <atomic>
#include <mutex>

#include <imgui.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/windowed_app_context.h>

#include "live/busy_notice_text.h"

namespace torchlight::live {

namespace {

rex::Runtime* g_runtime = nullptr;
rex::ui::WindowedAppContext* g_app_context = nullptr;

std::mutex g_mutex;
std::string g_text;  // under g_mutex
std::atomic<size_t> g_done{0}, g_total{0};
std::atomic<bool> g_shown{false};
rex::ui::ImGuiDialog* g_dialog = nullptr;  // UI thread

class NoticeDialog : public rex::ui::ImGuiDialog {
 public:
  explicit NoticeDialog(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    std::string text;
    {
      std::lock_guard lock(g_mutex);
      text = g_text;
    }
    const std::string line = BusyNoticeLine(text, g_done.load(), g_total.load());
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.85f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.7f);
    ImGui::Begin("##busy_notice", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoSavedSettings);
    // Readable at the window's size: the font's height a 30th of the window's.
    ImGui::SetWindowFontScale(std::max(1.0f, io.DisplaySize.y / 30.0f / ImGui::GetFontSize()));
    ImGui::TextUnformatted(line.c_str());
    ImGui::SetWindowFontScale(1.0f);
    ImGui::End();
  }
};

// UI thread: the dialog as g_shown says.
void Sync() {
  const bool shown = g_shown.load();
  if (shown && !g_dialog) {
    rex::ui::ImGuiDrawer* drawer = g_runtime ? g_runtime->imgui_drawer() : nullptr;
    if (!drawer) {
      REXLOG_WARN("busy notice: no UI drawer; not shown");
      return;
    }
    g_dialog = new NoticeDialog(drawer);
  } else if (!shown && g_dialog) {
    delete g_dialog;  // removes itself from the drawer
    g_dialog = nullptr;
  }
}

void PostSync() {
  if (g_app_context) g_app_context->CallInUIThread([] { Sync(); });
}

}  // namespace

void InstallBusyNotice(rex::Runtime* runtime, rex::ui::WindowedAppContext* app_context) {
  g_runtime = runtime;
  g_app_context = app_context;
}

void ShowBusyNotice(std::string text) {
  {
    std::lock_guard lock(g_mutex);
    g_text = std::move(text);
  }
  g_done = 0;
  g_total = 0;
  g_shown = true;
  PostSync();
}

void SetBusyNoticeProgress(size_t done, size_t total) {
  g_total = total;
  g_done = done;
}

void HideBusyNotice() {
  g_shown = false;
  PostSync();
}

bool BusyNoticeShown() { return g_shown.load(); }

rex::ui::ImGuiDialog* BusyNoticeDialog() { return g_dialog; }

}  // namespace torchlight::live
