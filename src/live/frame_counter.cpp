#include "live/frame_counter.h"

#include <algorithm>
#include <atomic>
#include <cstdio>

#include <imgui.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/keybinds.h>

#include "live/frame_timing.h"

namespace torchlight::live {

FrameCounterStats SummarizeFrames(const std::vector<float>& frame_ms) {
  FrameCounterStats s;
  s.frames = frame_ms.size();
  if (frame_ms.empty()) return s;
  s.last_ms = frame_ms.back();
  // Frame rate over the last second: the frames whose times add up to it, newest first.
  double second = 0;
  size_t in_second = 0;
  for (auto it = frame_ms.rbegin(); it != frame_ms.rend() && second < 1000.0; ++it) {
    second += *it;
    ++in_second;
  }
  if (second > 0) s.fps = 1000.0 * double(in_second) / second;
  std::vector<float> sorted = frame_ms;
  std::sort(sorted.begin(), sorted.end());
  s.max_ms = sorted.back();
  const float p99 = sorted[std::min(sorted.size() - 1, size_t(0.99 * double(sorted.size())))];
  if (p99 > 0) s.low_1pct_fps = 1000.0 / p99;
  s.over_33 = size_t(sorted.end() - std::upper_bound(sorted.begin(), sorted.end(), 33.0f));
  s.over_50 = size_t(sorted.end() - std::upper_bound(sorted.begin(), sorted.end(), 50.0f));
  return s;
}

namespace {

rex::Runtime* g_runtime = nullptr;
std::atomic<rex::ui::ImGuiDialog*> g_dialog{nullptr};

// The graph's frames and scale: about the last 5 s at 60 fps, 0 to 50 ms (a full-height spike is a
// three-frame stutter or worse).
constexpr size_t kGraphFrames = 300;
constexpr float kGraphMaxMs = 50.0f;

class CounterDialog : public rex::ui::ImGuiDialog {
 public:
  explicit CounterDialog(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    const std::vector<float> frames = FrameTiming::Get().Recent();
    const FrameCounterStats s = SummarizeFrames(frames);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 20, 20), ImGuiCond_Always, ImVec2(1, 0));
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGui::Begin("##frame_counter", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
    ImGui::SetWindowFontScale(2.5f);
    ImGui::Text("%.0f FPS", s.fps);
    ImGui::SetWindowFontScale(1.3f);
    ImGui::Text("%.1f ms", s.last_ms);
    ImGui::Text("1%% low %.0f FPS", s.low_1pct_fps);
    ImGui::Text("max %.0f ms  >33 ms %zu  >50 ms %zu", s.max_ms, s.over_33, s.over_50);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Text("last %lld s", static_cast<long long>(FrameTiming::kRecent.count()));
    const size_t first = frames.size() > kGraphFrames ? frames.size() - kGraphFrames : 0;
    ImGui::PlotLines("##frame_times", frames.data() + first, int(frames.size() - first), 0,
                     "frame time, 0-50 ms", 0.0f, kGraphMaxMs, ImVec2(360, 90));
    ImGui::End();
  }
};

void Toggle() {
  if (rex::ui::ImGuiDialog* dialog = g_dialog.exchange(nullptr)) {
    delete dialog;  // removes itself from the drawer
    return;
  }
  rex::ui::ImGuiDrawer* drawer = g_runtime ? g_runtime->imgui_drawer() : nullptr;
  if (!drawer) {
    REXLOG_WARN("frame counter: no UI drawer yet");
    return;
  }
  g_dialog = new CounterDialog(drawer);
}

}  // namespace

void InstallFrameCounter(rex::Runtime* runtime) {
  g_runtime = runtime;
  // The SDK's debug overlay (Xenos) also takes F3 and has no frame times here: ours instead.
  rex::ui::UnregisterBind("bind_debug_overlay");
  rex::ui::RegisterBind("bind_frame_counter", "F3", "Toggle the frame rate counter", [] { Toggle(); });
}

rex::ui::ImGuiDialog* FrameCounterDialog() { return g_dialog.load(); }

}  // namespace torchlight::live
