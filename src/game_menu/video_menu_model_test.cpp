// Tests for the video settings column's model: texts, steps through the choices, the checkbox,
// and the restart notice.

#include <cstdio>
#include <cstdlib>

#include "game_menu/video_menu_model.h"

namespace {

using namespace torchlight::game_menu;
using namespace torchlight::settings;

void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    std::exit(1);
  }
}

Capabilities Desktop() {
  Capabilities caps;
  caps.render_systems = {"OpenGL 3+ Rendering Subsystem"};
  caps.display = {1920, 1080};
  caps.display_modes = {{1920, 1080}, {1600, 900}, {1280, 720}, {1280, 1024}};
  caps.display_hz = 144;
  caps.any_vblank_rate = true;
  caps.languages = {"en", "de", "fr", "es"};
  return caps;
}

void TestResolution() {
  VideoMenuModel m(Desktop(), HostSettings{}, HostSettings{});
  Check(m.Text(Row::kResolution) == "1920x1080", "window size shown as the display's");
  Check(!m.Step(Row::kResolution, -1), "first choice: left does nothing");
  Check(m.Step(Row::kResolution, +1) && m.Text(Row::kResolution) == "1600x900", "right: next");
  Check(m.settings().render_resolution == Resolution{1600, 900}, "stored as chosen");
  Check(m.Step(Row::kResolution, +1) && m.Text(Row::kResolution) == "1280x720", "1280x720");
  Check(!m.Step(Row::kResolution, +1), "last choice: right does nothing");
  m.Step(Row::kResolution, -1);
  m.Step(Row::kResolution, -1);
  Check(m.settings().render_resolution == Resolution{},
        "back to the display's size stores window again");
}

void TestFpsAndVsync() {
  VideoMenuModel m(Desktop(), HostSettings{}, HostSettings{});
  Check(m.Text(Row::kFpsCap) == "60", "default cap");
  Check(m.Step(Row::kFpsCap, -1) && m.Text(Row::kFpsCap) == "30", "left: 30");
  for (int i = 0; i < 20; ++i) m.Step(Row::kFpsCap, +1);
  Check(m.Text(Row::kFpsCap) == "Unlimited" && m.settings().fps_cap == 0, "last: unlimited");
  Check(m.Checked(Row::kVsync), "vsync on by default");
  Check(m.Toggle(Row::kVsync) && !m.settings().vsync && !m.Checked(Row::kVsync), "toggle off");
  Check(!m.Toggle(Row::kFpsCap), "only the checkbox toggles");
  Check(!m.RestartNeeded(), "none of these needs a restart");
}

void TestLanguageAndRestart() {
  HostSettings startup;
  VideoMenuModel m(Desktop(), startup, startup);
  Check(m.Text(Row::kLanguage) == "English", "default language");
  Check(m.Step(Row::kLanguage, +1) && m.Text(Row::kLanguage) == "Deutsch", "next: German");
  Check(m.RestartNeeded(), "language changed: restart");
  m.Step(Row::kLanguage, +1);
  m.Step(Row::kLanguage, +1);
  Check(m.Text(Row::kLanguage) == "Español" && m.settings().language == "es", "Spanish");
  m.Step(Row::kLanguage, -1);
  m.Step(Row::kLanguage, -1);
  m.Step(Row::kLanguage, -1);
  Check(m.settings().language.empty() && !m.RestartNeeded(),
        "back to English, the default language: stored empty, no restart");
  HostSettings spanish;
  spanish.language = "es";
  VideoMenuModel es(Desktop(), spanish, spanish);
  Check(!es.RestartNeeded(), "started in Spanish: no restart");
  // Started in French by the runtime (--user_language) with the setting empty.
  VideoMenuModel fr(Desktop(), HostSettings{}, HostSettings{}, "fr");
  Check(fr.Text(Row::kLanguage) == "Français", "empty setting shows the game's language");
  Check(fr.Step(Row::kLanguage, +1) && fr.settings().language == "es" && fr.RestartNeeded(),
        "next after French: Spanish, restart");
  Check(fr.Step(Row::kLanguage, -1) && fr.settings().language.empty() && !fr.RestartNeeded(),
        "back to French stores empty");
}

void TestGpu() {
  Capabilities caps = Desktop();
  caps.gpus = {{"0000:00:02.0", "Intel UHD Graphics 630", true},
               {"0000:01:00.0", "NVIDIA GeForce GTX 1050 Ti Mobile", false}};
  caps.gpu_selectable = true;
  VideoMenuModel m(caps, HostSettings{}, HostSettings{});
  Check(m.Enabled(Row::kGpu), "two GPUs: enabled");
  Check(m.Text(Row::kGpu) == "Intel UHD 630", "default GPU shown by its short name");
  Check(m.Step(Row::kGpu, +1) && m.settings().gpu == "0000:01:00.0", "NVIDIA chosen");
  Check(m.Text(Row::kGpu) == "NVIDIA GTX 1050 Ti" && m.RestartNeeded(), "named, needs a restart");
  Check(ShortGpuName("AMD Radeon RX 7900 XTX Phantom Gaming OC") == "AMD Radeon RX 7900...",
        "long names cut after a whole word");
  Check(ShortGpuName("AMD Custom GPU 0405") == "AMD Custom GPU 0405", "short names kept");
  Check(m.Step(Row::kGpu, -1) && m.settings().gpu.empty() && !m.RestartNeeded(),
        "back to the default stores empty");
  caps.gpu_selectable = false;
  VideoMenuModel locked(caps, HostSettings{}, HostSettings{});
  Check(!locked.Enabled(Row::kGpu) && !locked.Step(Row::kGpu, +1),
        "choice could not stick: disabled");
}

void TestAspect() {
  VideoMenuModel m(Desktop(), HostSettings{}, HostSettings{});
  Check(m.Enabled(Row::kAspect) && m.Text(Row::kAspect) == "Auto", "automatic by default");
  Check(!m.Step(Row::kAspect, -1), "automatic is the first choice");
  Check(m.Step(Row::kAspect, +1) && m.Text(Row::kAspect) == "4:3", "next: 4:3");
  Check(m.settings().aspect == Aspect::k4x3 && m.RestartNeeded(), "changed: restart");
  m.Step(Row::kAspect, +1);
  Check(m.Step(Row::kAspect, +1) && m.Text(Row::kAspect) == "16:9", "16:9");
  Check(!m.Step(Row::kAspect, +1), "nothing wider than the 16:9 display");
  for (int i = 0; i < 3; ++i) m.Step(Row::kAspect, -1);
  Check(m.settings().aspect == Aspect::kAuto && !m.RestartNeeded(), "back to automatic");
  HostSettings wide;
  wide.aspect = Aspect::k21x9;
  VideoMenuModel started_wide(Desktop(), HostSettings{}, wide);
  Check(started_wide.RestartNeeded(), "differs from the startup aspect: restart");
}

void TestAchievements() {
  VideoMenuModel m(Desktop(), HostSettings{}, HostSettings{});
  Check(m.Enabled(Row::kAchievements) && m.Text(Row::kAchievements) == "Xbox 360",
        "Xbox 360 by default (no key)");
  Check(!m.Step(Row::kAchievements, -1), "Xbox 360 is the first choice");
  Check(m.Step(Row::kAchievements, +1) && m.Text(Row::kAchievements) == "PC (incomplete)",
        "right: PC");
  Check(m.settings().achievements == AchievementSet::kPc && m.RestartNeeded(), "PC needs a restart");
  Check(!m.Step(Row::kAchievements, +1), "PC is the last choice");
  Check(m.Step(Row::kAchievements, -1) && m.settings().achievements == AchievementSet::kXbox &&
            !m.RestartNeeded(),
        "back to Xbox 360: explicit, no restart (same set as at startup)");
  HostSettings pc;
  pc.achievements = AchievementSet::kPc;
  VideoMenuModel started_pc(Desktop(), pc, pc);
  Check(started_pc.Text(Row::kAchievements) == "PC (incomplete)" && !started_pc.RestartNeeded(),
        "started with PC");
  Check(started_pc.Reset() && started_pc.Text(Row::kAchievements) == "Xbox 360" &&
            started_pc.RestartNeeded(),
        "Reset Defaults: Xbox 360, restart");
}

void TestReset() {
  HostSettings spanish;
  spanish.language = "es";
  VideoMenuModel m(Desktop(), spanish, spanish);
  m.Step(Row::kResolution, +1);
  m.Step(Row::kFpsCap, +1);
  m.Toggle(Row::kVsync);
  Check(m.Reset(), "reset changes things");
  Check(m.settings() == HostSettings{}, "back to the defaults");
  Check(m.RestartNeeded(), "default language differs from the startup one: restart");
  Check(!m.Reset(), "already at the defaults");
}

void TestDisabledRows() {
  VideoMenuModel m(Desktop(), HostSettings{}, HostSettings{});
  Check(!m.Enabled(Row::kRenderer) && !m.Enabled(Row::kGpu), "one renderer, no GPU choice");
  Check(m.Text(Row::kRenderer) == "OpenGL 3+", "renderer name without the suffix");
  Check(m.Text(Row::kGpu) == "Default", "default GPU");
  Check(!m.Step(Row::kRenderer, +1) && !m.Step(Row::kGpu, +1), "disabled rows do not move");
}

// Windows: no default GPU (automatic first), the choice only with Direct3D 11.
void TestWindowsGpu() {
  Capabilities caps = Desktop();
  caps.render_systems.push_back("Direct3D11 Rendering Subsystem");
  caps.gpu_render_systems = {"Direct3D11 Rendering Subsystem"};
  caps.gpus = {{"pci:10de:2520:09261028:a1:0", "NVIDIA GeForce RTX 3060 Laptop GPU", false},
               {"pci:8086:3e9b:09261028:02:0", "Intel(R) UHD Graphics 630", false}};
  caps.gpu_selectable = true;
  VideoMenuModel m(caps, HostSettings{}, HostSettings{});
  Check(!m.Enabled(Row::kGpu), "OpenGL (the default renderer): no GPU choice");
  Check(m.Text(Row::kGpu) == "Auto", "automatic shown");
  Check(m.Step(Row::kRenderer, +1) && m.Enabled(Row::kGpu), "Direct3D 11: the GPU row enabled");
  Check(m.Step(Row::kGpu, +1) && m.settings().gpu == "pci:10de:2520:09261028:a1:0",
        "after automatic, the first GPU");
  Check(m.settings().gpu_name == "NVIDIA GeForce RTX 3060 Laptop GPU", "its name stored too");
  Check(m.Step(Row::kGpu, -1) && m.settings().gpu.empty() && m.settings().gpu_name.empty() &&
            m.Text(Row::kGpu) == "Auto",
        "back to automatic");

  // Only software adapters (no GPU driver): nothing to choose, automatic.
  caps.gpus.clear();
  VideoMenuModel software(caps, HostSettings{}, HostSettings{});
  software.Step(Row::kRenderer, +1);
  Check(!software.Enabled(Row::kGpu) && software.settings().gpu.empty(),
        "software only: no GPU choice");
}

void TestRendererChoice() {
  Capabilities caps = Desktop();
  caps.render_systems.push_back("Direct3D11 Rendering Subsystem");
  VideoMenuModel m(caps, HostSettings{}, HostSettings{});
  Check(m.Enabled(Row::kRenderer), "two renderers: the row is enabled");
  Check(m.Step(Row::kRenderer, +1), "right: the next renderer");
  Check(m.settings().render_system == "Direct3D11 Rendering Subsystem", "stored with OGRE's name");
  Check(m.Text(Row::kRenderer) == "Direct3D11", "a chosen renderer is shown without the suffix");
}

// A session that fell back: GL3+ saved, but this machine has no OpenGL 3.3, so Direct3D 11 runs.
void TestRendererUnavailable() {
  const std::string d3d11 = "Direct3D11 Rendering Subsystem";
  const std::string gl = "OpenGL 3+ Rendering Subsystem";
  Capabilities caps = Desktop();
  caps.render_systems = {d3d11, gl};
  caps.unavailable_render_systems = {gl};
  caps.gpu_render_systems = {d3d11};
  caps.gpus = {{"pci:10de:2520:09261028:a1:0", "NVIDIA GeForce RTX 3060 Laptop GPU", false},
               {"pci:8086:3e9b:09261028:02:0", "Intel(R) UHD Graphics 630", false}};
  caps.gpu_selectable = true;
  HostSettings saved;
  saved.render_system = gl;
  VideoMenuModel m(caps, saved, saved);
  Check(m.Text(Row::kRenderer) == "OpenGL 3+ (unavailable)", "the saved choice shown, marked");
  Check(m.Enabled(Row::kRenderer), "the player can still choose one that runs");
  Check(m.Enabled(Row::kGpu), "the GPU choice applies to the Direct3D 11 in use");

  // Another setting changed and saved: the saved render system stays GL3+.
  Check(m.Step(Row::kFpsCap, +1) && m.Toggle(Row::kVsync), "other settings change");
  Check(m.settings().render_system == gl, "the render system is not touched by other changes");
  Check(Serialize(m.settings()).find("render_system = \"" + gl + "\"") != std::string::npos,
        "settings.toml keeps GL3+");
  Check(!m.RestartNeeded(), "nothing that needs a restart changed");

  // Only the player's own choice changes it; GL3+ cannot be chosen back here.
  Check(m.Step(Row::kRenderer, -1) && m.settings().render_system == d3d11,
        "choosing a renderer by hand stores Direct3D 11");
  Check(m.Text(Row::kRenderer) == "Direct3D11" && !m.Enabled(Row::kRenderer),
        "Direct3D 11 is now the only choice");
  Check(!m.Step(Row::kRenderer, +1) && m.settings().render_system == d3d11,
        "GL3+ is not offered");
  Check(m.RestartNeeded(), "a renderer change needs a restart");
}

}  // namespace

int main() {
  TestResolution();
  TestFpsAndVsync();
  TestLanguageAndRestart();
  TestDisabledRows();
  TestRendererChoice();
  TestRendererUnavailable();
  TestWindowsGpu();
  TestReset();
  TestGpu();
  TestAspect();
  TestAchievements();
  std::printf("video_menu_model_test: ok\n");
  return 0;
}
