// Tests for the host settings: file round trip, tolerant parsing, menu choices per machine and
// normalization against what the machine offers.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "settings/host_settings.h"

namespace {

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
  caps.gpus = {{"pci-0000:00:02.0", "Intel"}, {"pci-0000:01:00.0", "NVIDIA"}};
  caps.gpu_selectable = true;
  caps.display = {1920, 1080};
  caps.display_modes = {{1920, 1080}, {1680, 1050}, {1600, 900}, {1366, 768}, {1280, 720},
                        {1280, 1024}, {800, 600}, {1600, 900}};
  caps.display_hz = 144;
  caps.any_vblank_rate = true;
  return caps;
}

Capabilities SteamDeck() {
  Capabilities caps;
  caps.render_systems = {"OpenGL 3+ Rendering Subsystem"};
  caps.gpus = {{"pci-0000:04:00.0", "AMD Custom GPU 0405"}};
  caps.gpu_selectable = true;
  caps.display = {1280, 800};
  caps.display_modes = {{1280, 800}, {800, 500}};
  caps.display_hz = 60;
  return caps;
}

void TestRoundTrip() {
  HostSettings s;
  s.render_system = "OpenGL 3+ Rendering Subsystem";
  s.gpu = "pci-0000:01:00.0";
  s.gpu_name = "NVIDIA GeForce RTX 3060 Laptop GPU";
  s.render_resolution = {2560, 1440};
  s.fps_cap = 144;
  s.vsync = false;
  s.language = "es";
  s.aspect = Aspect::k21x9;
  s.achievements = AchievementSet::kPc;
  std::vector<std::string> warnings;
  Check(Parse(Serialize(s), &warnings) == s, "serialize/parse round trip");
  Check(warnings.empty(), "no warnings on our own output");
  Check(Parse(Serialize(HostSettings{})) == HostSettings{}, "defaults round trip");
  // Quotes and backslashes in strings.
  s.gpu = "a \"quoted\" \\ id # not a comment";
  Check(Parse(Serialize(s)) == s, "escaped strings round trip");
}

void TestAchievementSet() {
  std::vector<std::string> warnings;
  Check(EffectiveAchievements(Parse("", &warnings)) == AchievementSet::kXbox && warnings.empty(),
        "no key: Xbox, no warning");
  Check(!Parse("").achievements, "no key: not set");
  Check(EffectiveAchievements(Parse("achievements = \"pc\"\n")) == AchievementSet::kPc, "pc");
  Check(EffectiveAchievements(Parse("achievements = \"xbox\"\n")) == AchievementSet::kXbox, "xbox");
  warnings.clear();
  HostSettings bad = Parse("achievements = \"steam\"\n", &warnings);
  Check(!bad.achievements && EffectiveAchievements(bad) == AchievementSet::kXbox && warnings.size() == 1,
        "invalid value: Xbox and a warning");
  Check(Parse(Serialize(HostSettings{})) == HostSettings{} &&
            Serialize(HostSettings{}).find("# achievements = \"xbox\"") != std::string::npos,
        "unset is not written, only shown as a comment");
  Check(RequiresRestart(Setting::kAchievements), "the achievement set needs a restart");
}

void TestTolerantParse() {
  std::vector<std::string> warnings;
  HostSettings s = Parse(
      "# comment\n"
      "\n"
      "fps_cap = 120   # trailing comment\r\n"
      "vsync=false\n"
      "render_resolution = \"window\"\n"
      "colour = \"blue\"\n"
      "fps_cap_typo 30\n"
      "gpu = unquoted\n"
      "render_resolution = \"0x720\"\n",
      &warnings);
  Check(s.fps_cap == 120, "integer with trailing comment and CRLF");
  Check(!s.vsync, "bool without spaces");
  Check(s.render_resolution == Resolution{}, "window resolution, bad one ignored");
  Check(s.gpu.empty(), "unquoted string ignored");
  Check(warnings.size() == 4, "unknown key, no '=', unquoted string, zero size reported");
}

void TestFiles() {
  const auto dir = std::filesystem::temp_directory_path() / "tl_host_settings_test";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  const std::string path = (dir / std::string(kFileName)).string();
  HostSettings s;
  std::vector<std::string> warnings;
  std::string error;
  s.fps_cap = 0;
  Check(Load(path, s, warnings, error) && s == HostSettings{}, "missing file gives defaults");
  s.fps_cap = 0;
  s.render_resolution = {1600, 900};
  Check(Save(path, s, error), "save");
  Check(!std::filesystem::exists(path + ".tmp"), "no temporary file left");
  HostSettings loaded;
  Check(Load(path, loaded, warnings, error) && loaded == s, "load what was saved");
  Check(!Save((dir / "missing" / "x.toml").string(), s, error) && !error.empty(),
        "save into a missing directory fails with a message");
  std::filesystem::remove_all(dir);
}

void TestChoices() {
  const Capabilities desktop = Desktop();
  auto res = ResolutionChoices(desktop);
  Check(res.enabled, "desktop resolutions enabled");
  Check(res.values.front() == Resolution{1920, 1080}, "display size first");
  Check(res.values.size() == 4, "16:9 modes only (1366x768 counts), duplicates removed");
  Check(res.values.back() == Resolution{1280, 720}, "smallest last");
  auto fps = FpsCapChoices(desktop);
  Check(fps.values.back() == 0, "unlimited last");
  Check(std::count(fps.values.begin(), fps.values.end(), 144u) == 1, "display rate not duplicated");
  Check(GpuChoices(desktop).enabled, "two GPUs selectable");

  const Capabilities deck = SteamDeck();
  Check(!GpuChoices(deck).enabled, "one GPU: disabled");
  Check(!RenderSystemChoices(deck).enabled, "one render system: disabled");
  Check(ResolutionChoices(deck).values.size() == 2, "16:10 modes on the Deck");
  fps = FpsCapChoices(deck);
  Check(fps.values == std::vector<uint32_t>{60, 0}, "without any vblank rate: 60 and unlimited");

  Capabilities hybrid_unselectable = desktop;
  hybrid_unselectable.gpu_selectable = false;
  Check(!GpuChoices(hybrid_unselectable).enabled, "platform cannot choose: disabled");

  Check(RequiresRestart(Setting::kRenderSystem) && RequiresRestart(Setting::kGpu),
        "render system and GPU need a restart");
  Check(!RequiresRestart(Setting::kRenderResolution) && !RequiresRestart(Setting::kFpsCap) &&
            !RequiresRestart(Setting::kVsync),
        "resolution, cap and vsync apply live");
}

void TestNormalize() {
  HostSettings s;
  s.render_system = "Vulkan Rendering Subsystem";
  s.gpu = "pci-0000:01:00.0";
  s.render_resolution = {1600, 900};
  s.fps_cap = 144;
  HostSettings deck = Normalize(s, SteamDeck());
  Check(deck.render_system.empty(), "render system not offered: automatic");
  Check(deck.gpu.empty(), "GPU not choosable: automatic");
  Check(deck.render_resolution == Resolution{}, "resolution not offered: window");
  Check(deck.fps_cap == 60, "cap not offered: 60");
  s.render_system = "OpenGL 3+ Rendering Subsystem";
  s.gpu_name = "NVIDIA";
  Check(Normalize(s, Desktop()) == s, "everything offered: unchanged");
  s.gpu_name = "an old name";
  Check(Normalize(s, Desktop()).gpu_name == "NVIDIA", "the name follows the GPU's");

  // Windows: a GPU no longer in the machine falls back to automatic, its name too.
  Capabilities windows = Desktop();
  windows.gpus = {{"pci:10de:2520:09261028:a1:0", "NVIDIA", false},
                  {"pci:8086:3e9b:09261028:02:0", "Intel", false}};
  s.gpu = "pci:1002:73bf:00000000:c1:0";
  s.gpu_name = "AMD Radeon RX 6800 XT";
  const HostSettings gone = Normalize(s, windows);
  Check(gone.gpu.empty() && gone.gpu_name.empty(), "GPU gone: automatic");
  Check(GpuChoices(windows).values.front().empty(), "no default GPU: automatic comes first");
  windows.gpus.clear();
  Check(!GpuChoices(windows).enabled && GpuChoices(windows).values.empty(),
        "software adapters only: nothing offered");
}

void TestLanguages() {
  const auto root = std::filesystem::temp_directory_path() / "tl_host_settings_languages";
  std::filesystem::remove_all(root);
  for (const char* code : {"fr", "de", "es", "uk", "it", "PT-BR", "ru"}) {
    std::filesystem::create_directories(root / "translations" / code);
  }
  for (const char* code : {"fr", "de", "uk", "PT-BR", "ru"}) {
    std::ofstream(root / "translations" / code / "translation.dat.adm") << "x";
  }
  std::ofstream(root / "translations" / "es" / "translation.dat") << "x";
  // "it" has a folder (its pak) but no translation file.
  Check(InstalledLanguages(root) ==
            std::vector<std::string>{"en", "de", "fr", "es", "pt-br", "ru", "uk"},
        "English, the game's own in console order, then packs by code (lower case)");
  Check(NativeLanguage("es") && !NativeLanguage("ru") && !NativeLanguage("en"), "native set");
  Check(InstalledLanguages(root / "missing") == std::vector<std::string>{"en"},
        "no translations: English only");
  std::filesystem::remove_all(root);

  Check(ConsoleLanguageId("en") == 1u && ConsoleLanguageId("de") == 3u &&
            ConsoleLanguageId("fr") == 4u && ConsoleLanguageId("es") == 5u,
        "console language ids");
  Check(!ConsoleLanguageId("xx").has_value(), "unknown code");
  Check(LanguageCode(5) == "es" && LanguageCode(1) == "en" && LanguageCode(99) == "en",
        "codes of console language ids");

  Capabilities caps = SteamDeck();
  caps.languages = {"en", "es"};
  Check(LanguageChoices(caps).enabled, "two languages: enabled");
  HostSettings s;
  s.language = "fr";
  Check(Normalize(s, caps).language.empty(), "language not installed: default");
  s.language = "es";
  Check(Normalize(s, caps).language == "es", "installed language kept");
  Check(RequiresRestart(Setting::kLanguage), "language needs a restart");
}

void TestAspect() {
  using V = std::vector<Aspect>;
  Check(HostSettings{}.aspect == Aspect::kAuto, "automatic by default");
  Check(RequiresRestart(Setting::kAspect), "aspect needs a restart");
  Check(AspectChoices(Desktop()).values ==
            V{Aspect::kAuto, Aspect::k4x3, Aspect::k16x10, Aspect::k16x9},
        "16:9 display: nothing wider");
  Check(AspectChoices(SteamDeck()).values == V{Aspect::kAuto, Aspect::k4x3, Aspect::k16x10},
        "16:10 display");
  Capabilities caps = Desktop();
  caps.display = {2560, 1080};
  Check(AspectChoices(caps).values.back() == Aspect::k21x9, "2560x1080 offers 21:9");
  caps.display = {5120, 1440};
  Check(AspectChoices(caps).values.back() == Aspect::k32x9, "32:9 display offers all");
  caps.display = {1280, 1024};
  // Automatic gives 4:3 there (hooks::VideoModeForAspect); 4:3 itself is wider than the display.
  Check(AspectChoices(caps).values == V{Aspect::kAuto} && !AspectChoices(caps).enabled,
        "5:4 display: automatic only");
  caps.display = {};
  Check(AspectChoices(caps).values.size() == 6, "unknown display: all");
  Check(AspectChoices(Desktop()).enabled, "enabled");

  HostSettings s;
  s.aspect = Aspect::k32x9;
  Check(Normalize(s, Desktop()).aspect == Aspect::kAuto, "wider than the display: automatic");
  s.aspect = Aspect::k4x3;
  Check(Normalize(s, Desktop()).aspect == Aspect::k4x3, "narrower kept");

  Check(AspectRatio(Aspect::kAuto, {2560, 1080}) == 2560.0 / 1080.0, "automatic: the display's");
  Check(AspectRatio(Aspect::kAuto, {}) == 0, "automatic, unknown display: 0");
  Check(AspectRatio(Aspect::k16x10, {2560, 1080}) == 1.6, "fixed: its own");
  Check(AspectName(Aspect::k21x9) == "21:9" && AspectName(Aspect::kAuto) == "auto", "names");

  std::vector<std::string> warnings;
  Check(Parse("aspect = \"16:10\"\n", &warnings).aspect == Aspect::k16x10 && warnings.empty(),
        "parse an aspect");
  Check(Parse("aspect = \"5:4\"\n", &warnings).aspect == Aspect::kAuto && warnings.size() == 1,
        "unknown aspect: automatic, reported");
}

// Windows without OpenGL 3.3: GL3+ installed but unavailable, so a session uses Direct3D 11.
void TestRenderSystemFallback() {
  const std::string d3d11 = "Direct3D11 Rendering Subsystem";
  const std::string gl = "OpenGL 3+ Rendering Subsystem";
  Capabilities caps = Desktop();
  caps.render_systems = {d3d11, gl};
  caps.gpu_render_systems = {d3d11};

  // GL 3.3 present: every choice is used as chosen.
  Check(EffectiveRenderSystem(caps, gl) == gl, "GL3+ with OpenGL 3.3: GL3+");
  Check(EffectiveRenderSystem(caps, d3d11) == d3d11, "Direct3D 11 chosen: Direct3D 11");
  Check(EffectiveRenderSystem(caps, "") == d3d11, "automatic: the first offered");
  Check(RenderSystemChoices(caps).values.size() == 2 && RenderSystemChoices(caps).enabled,
        "both offered with OpenGL 3.3");
  Check(!GpuChoiceApplies(caps, gl), "GPU choice does not apply to GL3+");

  // Without GL 3.3.
  caps.unavailable_render_systems = {gl};
  Check(EffectiveRenderSystem(caps, gl) == d3d11, "GL3+ without OpenGL 3.3: Direct3D 11");
  Check(EffectiveRenderSystem(caps, d3d11) == d3d11, "Direct3D 11 stays Direct3D 11");
  Check(EffectiveRenderSystem(caps, "") == d3d11, "automatic stays the first offered");
  Check(!RenderSystemUsable(caps, gl) && RenderSystemUsable(caps, d3d11), "usable ones");
  Check(RenderSystemChoices(caps).values == std::vector<std::string>{d3d11} &&
            !RenderSystemChoices(caps).enabled,
        "GL3+ not offered as a choice without OpenGL 3.3");
  Check(GpuChoiceApplies(caps, gl), "GPU choice applies to the Direct3D 11 the session uses");
  // The saved choice survives normalization, to be used again where GL 3.3 runs.
  HostSettings saved;
  saved.render_system = gl;
  Check(Normalize(saved, caps).render_system == gl, "Normalize keeps an unavailable choice");
  Check(Serialize(Normalize(saved, caps)).find(gl) != std::string::npos,
        "an unavailable choice is written back as it was");

  // Nothing usable (GL3+ only, without OpenGL 3.3: Linux never reports that).
  Capabilities gl_only = Desktop();
  gl_only.unavailable_render_systems = {gl};
  Check(EffectiveRenderSystem(gl_only, gl).empty(), "no render system can run: empty");
}

void TestRenderScale() {
  const Resolution guest{1280, 720};
  Check(RenderScaleFor({}, guest, {1920, 1080}) == 1.5f, "window size 1080p: 1.5");
  Check(RenderScaleFor({2560, 1440}, guest, {1920, 1080}) == 2.0f, "chosen 1440p: 2");
  Check(RenderScaleFor({}, guest, {1280, 800}) == 1.0f, "Deck 16:10: fits by width");
  Check(RenderScaleFor({960, 600}, guest, {1280, 800}) == 0.75f, "smaller than the guest");
  Check(RenderScaleFor({7680, 4320}, guest, {}) == 4.0f, "capped at 4");
  Check(RenderScaleFor({}, guest, {}) == 1.0f, "unknown window: 1");
}

}  // namespace

void TestGpuLogLabel() {
  const std::vector<Capabilities::Gpu> gpus = {{"0000:00:02.0", "Intel UHD", true},
                                                {"0000:01:00.0", "NVIDIA GTX", false}};
  HostSettings s;
  Check(GpuLogLabel(s, gpus) == "automatic", "no choice: automatic");
  s.gpu = "0000:01:00.0";
  Check(GpuLogLabel(s, gpus) == "NVIDIA GTX (0000:01:00.0)", "name from the machine, no stored name");
  s.gpu_name = "Old name";
  Check(GpuLogLabel(s, gpus) == "NVIDIA GTX (0000:01:00.0)", "the machine's name wins");
  s.gpu = "0000:02:00.0";
  Check(GpuLogLabel(s, gpus) == "Old name (0000:02:00.0)", "absent GPU: the stored name");
  s.gpu_name.clear();
  Check(GpuLogLabel(s, gpus) == "? (0000:02:00.0)", "absent GPU without a stored name");
}

int main() {
  TestAspect();
  TestRoundTrip();
  TestAchievementSet();
  TestTolerantParse();
  TestFiles();
  TestChoices();
  TestNormalize();
  TestRenderSystemFallback();
  TestRenderScale();
  TestLanguages();
  TestGpuLogLabel();
  std::printf("host_settings_test: ok\n");
  return 0;
}
