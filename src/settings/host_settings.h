// The host's own settings: what the game knows nothing about (render system, GPU, internal render
// resolution, frame rate cap, vsync). They live in a file of our own in the platform's
// configuration directory (platform::ConfigDir), not in the game's save; the game's settings stay
// in SAVE:\local_settings.txt.
//
// Read before the guest starts (render system and GPU are needed before anything loads GL), edited
// by the video menu. Nothing here depends on the platform or on OGRE: what a machine offers comes in
// as Capabilities, filled by the platform module and the backend.

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace torchlight::settings {

struct Resolution {
  uint32_t width = 0;
  uint32_t height = 0;
  bool operator==(const Resolution&) const = default;
};

// Aspect ratio of the game's frame: automatic (the display's) or a fixed one.
enum class Aspect { kAuto, k4x3, k16x10, k16x9, k21x9, k32x9 };

// Which achievement set the game uses: the Xbox 360 one (the game's own 12) or the PC one
// (achievements/, incomplete). Read once at startup.
enum class AchievementSet { kXbox, kPc };

// Stored values. Empty strings and zero sizes mean "automatic".
struct HostSettings {
  // OGRE render system name; empty = the first one available.
  std::string render_system;
  // Platform GPU id (Capabilities::Gpu::id); empty = the system's default (Windows: automatic).
  std::string gpu;
  // That GPU's name when it was chosen, to show and log it (also when it is no longer present).
  std::string gpu_name;
  // Internal render resolution; 0x0 = the window's size.
  Resolution render_resolution;
  // Frame rate cap = the guest's vblank frequency in Hz; 0 = unlimited.
  uint32_t fps_cap = 60;
  // Vertical sync of the backend's window.
  bool vsync = true;
  // Game language code ("en", "de", "fr", "es"...); empty = the runtime's default.
  std::string language;
  // Aspect ratio of the game's frame (only mode).
  Aspect aspect = Aspect::kAuto;
  // Achievement set; empty when the file has no valid "achievements" key (EffectiveAchievements:
  // Xbox). Not written back while empty.
  std::optional<AchievementSet> achievements;
  bool operator==(const HostSettings&) const = default;
};

// What this machine offers.
struct Capabilities {
  // Render systems installed and validated with replay.
  std::vector<std::string> render_systems;
  // Installed ones this machine cannot run (Windows without OpenGL 3.3: GL3+). Kept in
  // render_systems, so a saved choice of one survives Normalize and is used again where it runs;
  // a session falls back to another (EffectiveRenderSystem), and the menu cannot choose them.
  std::vector<std::string> unavailable_render_systems;
  struct Gpu {
    std::string id;
    std::string name;
    bool is_default = false;  // the one used when the setting is empty
  };
  std::vector<Gpu> gpus;
  // Render systems the GPU choice takes effect with (Windows: Direct3D 11 only); empty: all.
  std::vector<std::string> gpu_render_systems;
  // Whether the platform can make the choice of GPU stick (with one GPU it is moot anyway).
  bool gpu_selectable = false;
  // Native size of the display the game is shown on, and the display modes the platform reports.
  Resolution display;
  std::vector<Resolution> display_modes;
  // Refresh rate of that display in Hz (0 if unknown).
  uint32_t display_hz = 0;
  // Whether the guest's vblank can run at any rate (needs the runtime's support); without it only
  // 60 Hz and unlimited exist.
  bool any_vblank_rate = false;
  // Languages the user's game data has (InstalledLanguages).
  std::vector<std::string> languages;
};

// Settings the menu shows. Changing one of these takes effect after restarting the game.
enum class Setting {
  kRenderSystem, kGpu, kRenderResolution, kFpsCap, kVsync, kLanguage, kAspect, kAchievements
};
bool RequiresRestart(Setting setting);
// The achievement set in effect: the setting, or Xbox when it is missing or invalid.
inline AchievementSet EffectiveAchievements(const HostSettings& settings) {
  return settings.achievements.value_or(AchievementSet::kXbox);
}
// "xbox" / "pc".
std::string_view AchievementSetName(AchievementSet set);

// The GPU setting for the log: "automatic" without a choice, else "NAME (ID)", the name looked up
// in `gpus` by id, else the stored gpu_name (files written before gpu_name existed lack it), else
// "?".
std::string GpuLogLabel(const HostSettings& settings, const std::vector<Capabilities::Gpu>& gpus);

// Menu choices for one setting.
template <typename T>
struct Choices {
  std::vector<T> values;
  // False when there is nothing to choose (one value, or the platform cannot apply a choice).
  bool enabled = false;
};
// The ones that can run here (installed and not unavailable), in the order offered.
Choices<std::string> RenderSystemChoices(const Capabilities& caps);
// Installed and not unavailable.
bool RenderSystemUsable(const Capabilities& caps, const std::string& render_system);
// The render system a session uses for the chosen one: the choice when it can run here, else (empty,
// not installed, or unavailable) the first one offered that can; empty when none can.
std::string EffectiveRenderSystem(const Capabilities& caps, const std::string& render_system);
// Whether startup has to check that `checked` can run here before choosing the session's render
// system: the one the session would use (the saved `render_system`, or the first offered) is
// `checked`, and there is another to fall back to. Otherwise the check can wait until the settings
// menu lists the render systems (Windows: GL3+'s OpenGL 3.3 probe, ~150-180 ms with a GPU driver).
bool StartupNeedsRenderSystemCheck(const Capabilities& caps, const std::string& render_system,
                                   const std::string& checked);
// Whether the GPU choice takes effect with `render_system` (the one in effect for it).
bool GpuChoiceApplies(const Capabilities& caps, const std::string& render_system);
Choices<std::string> GpuChoices(const Capabilities& caps);  // ids
// Display modes with the display's aspect ratio, largest first; the first is the display's size.
Choices<Resolution> ResolutionChoices(const Capabilities& caps);
// Ascending, with 0 (unlimited) last.
Choices<uint32_t> FpsCapChoices(const Capabilities& caps);
Choices<std::string> LanguageChoices(const Capabilities& caps);
// Automatic first, then the fixed aspects no wider than the display, narrowest first (all of them
// when the display's size is unknown).
Choices<Aspect> AspectChoices(const Capabilities& caps);

// The aspect's name: "auto", "4:3", "16:10", "16:9", "21:9" or "32:9" (the file's values).
std::string_view AspectName(Aspect aspect);
// Width / height of the frame an aspect asks for: automatic is the display's, 0 when unknown.
double AspectRatio(Aspect aspect, Resolution display);

// Languages in the game data: English (the game's own text) plus every translations/<code>/ folder
// with a translation file the game can load (translation.dat, or its compiled translation.dat.adm).
// English first, then the game's own translations (NativeLanguage) in console language order, then
// language packs the user added, by code.
std::vector<std::string> InstalledLanguages(const std::filesystem::path& game_data_root);
// Whether the game picks this language's translation by itself from the console language (de, fr,
// es: guest_abi/game_ui.h kNativeTranslationLanguages), which the runtime answers from its
// user_language cvar. Other installed languages are language packs, loaded by pointing the game's
// "Translate File" at translations/<code>/.
bool NativeLanguage(std::string_view code);
// The console language id (XLanguage) of a code; nullopt for codes without one.
std::optional<uint32_t> ConsoleLanguageId(std::string_view code);
// The code of a console language id ("en" for unknown ids).
std::string LanguageCode(uint32_t console_language_id);

// The settings with every value this machine does not offer replaced by its automatic value (the
// frame rate cap falls back to 60).
HostSettings Normalize(const HostSettings& settings, const Capabilities& caps);

// The backend's internal render scale for a chosen render resolution (0x0: the window's size): the
// largest scale at which the guest's frame (guest_frame, which keeps its aspect ratio) fits in it,
// within the backend's range 0.25 to 4. 1 when a size is unknown.
float RenderScaleFor(Resolution chosen, Resolution guest_frame, Resolution window);

// The file format: "key = value" lines, a subset of TOML (strings in double quotes, integers,
// true/false, "#" comments). Unknown keys and malformed lines are skipped and reported in
// `warnings`; missing keys keep their defaults.
HostSettings Parse(std::string_view text, std::vector<std::string>* warnings = nullptr);
std::string Serialize(const HostSettings& settings);

// File access. Load returns the defaults when the file does not exist. Save writes a temporary file
// next to it and renames it over, so a crash never leaves half a file. Both report errors in
// `error` and return false on failure (Load still fills defaults).
inline constexpr std::string_view kFileName = "settings.toml";
bool Load(const std::string& path, HostSettings& settings, std::vector<std::string>& warnings,
          std::string& error);
bool Save(const std::string& path, const HostSettings& settings, std::string& error);

}  // namespace torchlight::settings
