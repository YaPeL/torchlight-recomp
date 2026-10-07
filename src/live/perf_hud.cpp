#include "live/perf_hud.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <imgui.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/windowed_app_context.h>

namespace torchlight::live::perf {
namespace {

using Clock = std::chrono::steady_clock;

// A step: what the player does, how long it lasts (0: until the next level load), and whether its
// frames are measured.
struct Step {
  const char* text;
  int seconds;
  bool measured;
};
// Startup: until the player is in the main menu; then the timed steps. A step with seconds 0 ends
// at the next level load (plus kSettle, so the loading screen is not measured).
constexpr Step kSteps[] = {
    {"Press Start and go to the main menu", 30, false},
    {"MAIN MENU: hands off", 40, true},
    {"New Character: create one and start", 0, false},
    {"TOWN: stand still", 40, true},
    {"TOWN: walk around the square", 40, true},
    {"Walk into the mine (Orden Mines)", 0, false},
    {"DUNGEON: stand still", 40, true},
    {"DUNGEON: walk and fight", 40, true},
    {"Done: quit the game", -1, false},
};
constexpr int kStepCount = int(sizeof(kSteps) / sizeof(kSteps[0]));
constexpr auto kSettle = std::chrono::seconds(3);

struct State {
  std::mutex mutex;
  int step = -1;  // -1: no frame yet
  Clock::time_point step_start{};
  bool waiting_load = false, load_seen = false;
  Clock::time_point load_time{};
  std::vector<double> step_frames;
  std::deque<Clock::time_point> recent;  // swaps of the last second, for the counter
  double last_frame_ms = 0;
};
State g;
std::atomic<rex::ui::ImGuiDialog*> g_hud{nullptr};
rex::ui::WindowedAppContext* g_app_context = nullptr;
rex::Runtime* g_runtime = nullptr;
std::atomic<bool> g_creating{false};

void Beep() {
  std::thread([] {
    if (std::system("pw-play /usr/share/sounds/freedesktop/stereo/complete.oga > /dev/null 2>&1") != 0) {
      std::fputc('\a', stderr);
    }
  }).detach();
}

// With the state locked: logs the finished step's frames, if measured.
void LogStep(int step) {
  if (step < 0 || !kSteps[step].measured || g.step_frames.empty()) return;
  std::vector<double> f = g.step_frames;
  std::sort(f.begin(), f.end());
  double sum = 0;
  for (double v : f) sum += v;
  const auto pct = [&](double p) { return f[std::min(f.size() - 1, size_t(p * f.size()))]; };
  const auto over = [&](double ms) { return f.end() - std::upper_bound(f.begin(), f.end(), ms); };
  REXLOG_INFO("perf step '{}': {} frames, {:.1f} fps, mean {:.2f} ms, p99 {:.2f}, max {:.2f}, "
              "over 33 ms {}, over 50 ms {}",
              kSteps[step].text, f.size(), 1000.0 * f.size() / sum, sum / f.size(), pct(0.99),
              f.back(), over(33.0), over(50.0));
}

// With the state locked.
void Advance(Clock::time_point now) {
  LogStep(g.step);
  g.step = std::min(g.step + 1, kStepCount - 1);
  g.step_start = now;
  g.step_frames.clear();
  g.waiting_load = kSteps[g.step].seconds == 0;
  g.load_seen = false;
  REXLOG_INFO("perf step {}: {}", g.step, kSteps[g.step].text);
  Beep();
}

class HudDialog : public rex::ui::ImGuiDialog {
 public:
  explicit HudDialog(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    double fps = 0, ms = 0;
    std::string step;
    int left = -1;
    {
      std::lock_guard lock(g.mutex);
      if (g.recent.size() > 1) {
        const double span =
            std::chrono::duration<double>(g.recent.back() - g.recent.front()).count();
        if (span > 0) fps = double(g.recent.size() - 1) / span;
      }
      ms = g.last_frame_ms;
      if (g.step >= 0) {
        const Step& s = kSteps[g.step];
        step = s.text;
        if (s.seconds > 0) {
          left = s.seconds - int(std::chrono::duration_cast<std::chrono::seconds>(
                                     Clock::now() - g.step_start).count());
        }
      }
    }
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 20, 20), ImGuiCond_Always, ImVec2(1, 0));
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGui::Begin("##perf_hud", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
    ImGui::SetWindowFontScale(3.0f);
    ImGui::Text("%.0f FPS", fps);
    ImGui::SetWindowFontScale(1.6f);
    ImGui::Text("%.1f ms", ms);
    if (!step.empty()) {
      ImGui::Separator();
      ImGui::Text("%s", step.c_str());
      if (left >= 0) ImGui::Text("%d s", left);
    }
    ImGui::End();
  }
};

}  // namespace

void Install(rex::ui::WindowedAppContext* app_context, rex::Runtime* runtime) {
  g_app_context = app_context;
  g_runtime = runtime;
}

rex::ui::ImGuiDialog* Hud() { return g_hud.load(); }

void OnSwap(double frame_ms) {
  const auto now = Clock::now();
  {
    std::lock_guard lock(g.mutex);
    g.last_frame_ms = frame_ms;
    g.recent.push_back(now);
    while (!g.recent.empty() && now - g.recent.front() > std::chrono::seconds(1)) {
      g.recent.pop_front();
    }
    if (g.step < 0) {
      Advance(now);
    } else {
      const Step& s = kSteps[g.step];
      if (s.measured) g.step_frames.push_back(frame_ms);
      if (s.seconds > 0 && now - g.step_start >= std::chrono::seconds(s.seconds)) {
        Advance(now);
      } else if (g.waiting_load && g.load_seen && now - g.load_time >= kSettle) {
        Advance(now);
      }
    }
  }
  if (!g_hud.load() && g_app_context && !g_creating.exchange(true)) {
    g_app_context->CallInUIThread([] {
      rex::ui::ImGuiDrawer* drawer = g_runtime ? g_runtime->imgui_drawer() : nullptr;
      if (drawer) {
        g_hud = new HudDialog(drawer);
      }
      g_creating = false;
    });
  }
}

void OnLevelLoaded() {
  std::lock_guard lock(g.mutex);
  if (g.waiting_load && !g.load_seen) {
    g.load_seen = true;
    g.load_time = Clock::now();
  }
}

}  // namespace torchlight::live::perf
