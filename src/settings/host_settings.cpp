#include "settings/host_settings.h"

#include "platform/durable_file.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace torchlight::settings {

namespace {

// Frame rate caps offered when the guest's vblank can run at any rate: common display rates.
constexpr uint32_t kStandardFpsCaps[] = {30, 60, 90, 120, 144, 165, 240};

// Console language ids (XLanguage, rex/system/xcontent.h) by code, in id order.
struct ConsoleLanguage {
  std::string_view code;
  uint32_t id;
};
constexpr ConsoleLanguage kConsoleLanguages[] = {
    {"en", 1}, {"ja", 2}, {"de", 3},     {"fr", 4},     {"es", 5},  {"it", 6},
    {"ko", 7}, {"zh-tw", 8}, {"pt", 9}, {"zh-cn", 10}, {"pl", 11}, {"ru", 12},
};

template <typename T>
bool Contains(const std::vector<T>& values, const T& value) {
  return std::find(values.begin(), values.end(), value) != values.end();
}

// The fixed aspects, narrowest first, with their names and ratios.
struct AspectInfo {
  Aspect aspect;
  std::string_view name;
  double ratio;
};
constexpr AspectInfo kAspects[] = {
    {Aspect::k4x3, "4:3", 4.0 / 3.0},     {Aspect::k16x10, "16:10", 16.0 / 10.0},
    {Aspect::k16x9, "16:9", 16.0 / 9.0},  {Aspect::k21x9, "21:9", 21.0 / 9.0},
    {Aspect::k32x9, "32:9", 32.0 / 9.0},
};

bool ParseAspect(std::string_view s, Aspect& out) {
  if (s == "auto") {
    out = Aspect::kAuto;
    return true;
  }
  for (const AspectInfo& info : kAspects) {
    if (info.name == s) {
      out = info.aspect;
      return true;
    }
  }
  return false;
}

// Same aspect ratio within 1% (1366x768 counts as 16:9).
bool SameAspect(Resolution a, Resolution b) {
  const double ra = double(a.width) / a.height;
  const double rb = double(b.width) / b.height;
  return std::abs(ra - rb) <= 0.01 * rb;
}

std::string_view Trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
  return s;
}

bool ParseUint(std::string_view s, uint32_t& out) {
  auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
  return ec == std::errc() && end == s.data() + s.size();
}

// A double-quoted string without escapes other than \" and \\.
bool ParseString(std::string_view s, std::string& out) {
  if (s.size() < 2 || s.front() != '"' || s.back() != '"') return false;
  out.clear();
  for (size_t i = 1; i + 1 < s.size(); ++i) {
    char c = s[i];
    if (c == '\\') {
      if (i + 2 >= s.size()) return false;
      c = s[++i];
      if (c != '"' && c != '\\') return false;
    } else if (c == '"') {
      return false;
    }
    out.push_back(c);
  }
  return true;
}

std::string Quote(std::string_view s) {
  std::string out = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') out.push_back('\\');
    out.push_back(c);
  }
  out.push_back('"');
  return out;
}

// "WIDTHxHEIGHT", or "window" for 0x0.
bool ParseResolution(std::string_view s, Resolution& out) {
  if (s == "window") {
    out = {};
    return true;
  }
  const size_t x = s.find('x');
  if (x == std::string_view::npos) return false;
  Resolution r;
  if (!ParseUint(s.substr(0, x), r.width) || !ParseUint(s.substr(x + 1), r.height)) return false;
  if (r.width == 0 || r.height == 0) return false;
  out = r;
  return true;
}

std::string FormatResolution(Resolution r) {
  if (r.width == 0 || r.height == 0) return "window";
  return std::to_string(r.width) + "x" + std::to_string(r.height);
}

}  // namespace

bool RequiresRestart(Setting setting) {
  switch (setting) {
    case Setting::kRenderSystem:
    case Setting::kGpu:
    case Setting::kLanguage:
    case Setting::kAspect:
    case Setting::kAchievements:
      return true;
    case Setting::kRenderResolution:
    case Setting::kFpsCap:
    case Setting::kVsync:
      return false;
  }
  return false;
}

bool RenderSystemUsable(const Capabilities& caps, const std::string& render_system) {
  return Contains(caps.render_systems, render_system) &&
         !Contains(caps.unavailable_render_systems, render_system);
}

std::string EffectiveRenderSystem(const Capabilities& caps, const std::string& render_system) {
  if (RenderSystemUsable(caps, render_system)) return render_system;
  for (const std::string& offered : caps.render_systems) {
    if (RenderSystemUsable(caps, offered)) return offered;
  }
  return "";
}

bool StartupNeedsRenderSystemCheck(const Capabilities& caps, const std::string& render_system,
                                   const std::string& checked) {
  if (EffectiveRenderSystem(caps, render_system) != checked) return false;
  for (const std::string& offered : caps.render_systems) {
    if (offered != checked && RenderSystemUsable(caps, offered)) return true;
  }
  return false;
}

Choices<std::string> RenderSystemChoices(const Capabilities& caps) {
  Choices<std::string> choices;
  for (const std::string& offered : caps.render_systems) {
    if (RenderSystemUsable(caps, offered)) choices.values.push_back(offered);
  }
  choices.enabled = choices.values.size() > 1;
  return choices;
}

bool GpuChoiceApplies(const Capabilities& caps, const std::string& render_system) {
  if (caps.gpu_render_systems.empty()) return true;
  return Contains(caps.gpu_render_systems, EffectiveRenderSystem(caps, render_system));
}

Choices<std::string> GpuChoices(const Capabilities& caps) {
  Choices<std::string> choices;
  // Without a default GPU (Windows) the first choice is automatic (empty).
  bool has_default = false;
  for (const auto& gpu : caps.gpus) has_default = has_default || gpu.is_default;
  if (!has_default && !caps.gpus.empty()) choices.values.push_back("");
  for (const auto& gpu : caps.gpus) choices.values.push_back(gpu.id);
  choices.enabled = caps.gpu_selectable && caps.gpus.size() > 1;
  return choices;
}

Choices<Resolution> ResolutionChoices(const Capabilities& caps) {
  Choices<Resolution> choices;
  const bool known_display = caps.display.width && caps.display.height;
  if (known_display) choices.values.push_back(caps.display);
  for (Resolution mode : caps.display_modes) {
    if (!mode.width || !mode.height) continue;
    if (known_display && !SameAspect(mode, caps.display)) continue;
    if (!Contains(choices.values, mode)) choices.values.push_back(mode);
  }
  std::stable_sort(choices.values.begin(), choices.values.end(),
                   [](Resolution a, Resolution b) { return a.width > b.width; });
  choices.enabled = choices.values.size() > 1;
  return choices;
}

Choices<uint32_t> FpsCapChoices(const Capabilities& caps) {
  Choices<uint32_t> choices;
  if (caps.any_vblank_rate) {
    choices.values.assign(std::begin(kStandardFpsCaps), std::end(kStandardFpsCaps));
    if (caps.display_hz && !Contains(choices.values, caps.display_hz)) {
      choices.values.push_back(caps.display_hz);
      std::sort(choices.values.begin(), choices.values.end());
    }
  } else {
    choices.values = {60};
  }
  choices.values.push_back(0);
  choices.enabled = true;
  return choices;
}

Choices<std::string> LanguageChoices(const Capabilities& caps) {
  Choices<std::string> choices;
  choices.values = caps.languages;
  choices.enabled = choices.values.size() > 1;
  return choices;
}

Choices<Aspect> AspectChoices(const Capabilities& caps) {
  Choices<Aspect> choices;
  choices.values.push_back(Aspect::kAuto);
  const double display = AspectRatio(Aspect::kAuto, caps.display);
  for (const AspectInfo& info : kAspects) {
    // Within 1% counts as the display's own (2560x1080 is 21:9).
    if (display == 0 || info.ratio <= display * 1.01) choices.values.push_back(info.aspect);
  }
  choices.enabled = choices.values.size() > 1;
  return choices;
}

std::string_view AspectName(Aspect aspect) {
  for (const AspectInfo& info : kAspects) {
    if (info.aspect == aspect) return info.name;
  }
  return "auto";
}

double AspectRatio(Aspect aspect, Resolution display) {
  for (const AspectInfo& info : kAspects) {
    if (info.aspect == aspect) return info.ratio;
  }
  if (!display.width || !display.height) return 0;
  return double(display.width) / double(display.height);
}

std::optional<uint32_t> ConsoleLanguageId(std::string_view code) {
  for (const ConsoleLanguage& language : kConsoleLanguages) {
    if (language.code == code) return language.id;
  }
  return std::nullopt;
}

std::string LanguageCode(uint32_t console_language_id) {
  for (const ConsoleLanguage& language : kConsoleLanguages) {
    if (language.id == console_language_id) return std::string(language.code);
  }
  return "en";
}

bool NativeLanguage(std::string_view code) {
  return code == "de" || code == "fr" || code == "es";
}

std::vector<std::string> InstalledLanguages(const std::filesystem::path& game_data_root) {
  std::vector<std::string> native, packs;
  std::error_code ec;
  for (const auto& entry :
       std::filesystem::directory_iterator(game_data_root / "translations", ec)) {
    if (!entry.is_directory(ec)) continue;
    std::string code = entry.path().filename().string();
    std::transform(code.begin(), code.end(), code.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    if (code == "en" || code.empty()) continue;
    if (!std::filesystem::exists(entry.path() / "translation.dat.adm", ec) &&
        !std::filesystem::exists(entry.path() / "translation.dat", ec)) {
      continue;
    }
    (NativeLanguage(code) ? native : packs).push_back(code);
  }
  auto by_console_id = [](const std::string& a, const std::string& b) {
    return ConsoleLanguageId(a).value_or(99) < ConsoleLanguageId(b).value_or(99);
  };
  std::sort(native.begin(), native.end(), by_console_id);
  std::sort(packs.begin(), packs.end());
  std::vector<std::string> languages = {"en"};
  languages.insert(languages.end(), native.begin(), native.end());
  languages.insert(languages.end(), packs.begin(), packs.end());
  return languages;
}

HostSettings Normalize(const HostSettings& settings, const Capabilities& caps) {
  HostSettings out = settings;
  if (!Contains(caps.render_systems, out.render_system)) out.render_system.clear();
  if (!GpuChoices(caps).enabled || !Contains(GpuChoices(caps).values, out.gpu)) out.gpu.clear();
  out.gpu_name.clear();
  for (const auto& gpu : caps.gpus) {
    if (!out.gpu.empty() && gpu.id == out.gpu) out.gpu_name = gpu.name;
  }
  if (out.render_resolution != Resolution{} &&
      !Contains(ResolutionChoices(caps).values, out.render_resolution)) {
    out.render_resolution = {};
  }
  if (!Contains(FpsCapChoices(caps).values, out.fps_cap)) out.fps_cap = HostSettings{}.fps_cap;
  if (!Contains(caps.languages, out.language)) out.language.clear();
  if (!Contains(AspectChoices(caps).values, out.aspect)) out.aspect = Aspect::kAuto;
  return out;
}

float RenderScaleFor(Resolution chosen, Resolution guest_frame, Resolution window) {
  const Resolution target = chosen == Resolution{} ? window : chosen;
  if (!target.width || !target.height || !guest_frame.width || !guest_frame.height) return 1;
  const float scale = std::min(float(target.width) / guest_frame.width,
                               float(target.height) / guest_frame.height);
  return std::clamp(scale, 0.25f, 4.0f);
}

std::string GpuLogLabel(const HostSettings& settings, const std::vector<Capabilities::Gpu>& gpus) {
  if (settings.gpu.empty()) return "automatic";
  std::string name = settings.gpu_name;
  for (const auto& gpu : gpus) {
    if (gpu.id == settings.gpu && !gpu.name.empty()) name = gpu.name;
  }
  return (name.empty() ? "?" : name) + " (" + settings.gpu + ")";
}

std::string_view AchievementSetName(AchievementSet set) {
  return set == AchievementSet::kPc ? "pc" : "xbox";
}

HostSettings Parse(std::string_view text, std::vector<std::string>* warnings) {
  HostSettings settings;
  auto warn = [&](int line, std::string message) {
    if (warnings) warnings->push_back("line " + std::to_string(line) + ": " + message);
  };
  int line_number = 0;
  while (!text.empty()) {
    const size_t eol = text.find('\n');
    std::string_view line = text.substr(0, eol);
    text = eol == std::string_view::npos ? std::string_view{} : text.substr(eol + 1);
    ++line_number;
    // A "#" outside a string starts a comment.
    bool in_string = false;
    for (size_t i = 0; i < line.size(); ++i) {
      if (line[i] == '\\' && in_string) {
        ++i;
      } else if (line[i] == '"') {
        in_string = !in_string;
      } else if (line[i] == '#' && !in_string) {
        line = line.substr(0, i);
        break;
      }
    }
    line = Trim(line);
    if (line.empty()) continue;
    const size_t eq = line.find('=');
    if (eq == std::string_view::npos) {
      warn(line_number, "not a \"key = value\" line");
      continue;
    }
    const std::string_view key = Trim(line.substr(0, eq));
    const std::string_view value = Trim(line.substr(eq + 1));
    bool ok = true;
    if (key == "render_system") {
      ok = ParseString(value, settings.render_system);
    } else if (key == "gpu") {
      ok = ParseString(value, settings.gpu);
    } else if (key == "gpu_name") {
      ok = ParseString(value, settings.gpu_name);
    } else if (key == "render_resolution") {
      std::string s;
      ok = ParseString(value, s) && ParseResolution(s, settings.render_resolution);
    } else if (key == "fps_cap") {
      ok = ParseUint(value, settings.fps_cap);
    } else if (key == "language") {
      ok = ParseString(value, settings.language);
    } else if (key == "aspect") {
      std::string s;
      ok = ParseString(value, s) && ParseAspect(s, settings.aspect);
    } else if (key == "achievements") {
      std::string s;
      ok = ParseString(value, s) && (s == "xbox" || s == "pc");
      if (ok) settings.achievements = s == "pc" ? AchievementSet::kPc : AchievementSet::kXbox;
    } else if (key == "vsync") {
      ok = value == "true" || value == "false";
      if (ok) settings.vsync = value == "true";
    } else {
      warn(line_number, "unknown key \"" + std::string(key) + "\"");
      continue;
    }
    if (!ok) warn(line_number, "bad value for \"" + std::string(key) + "\"");
  }
  return settings;
}

std::string Serialize(const HostSettings& settings) {
  std::ostringstream out;
  out << "# Torchlight host settings (the game's own settings are in its save).\n"
      << "# Empty strings mean automatic.\n\n"
      << "# Render system; takes effect after a restart.\n"
      << "render_system = " << Quote(settings.render_system) << "\n"
      << "# GPU; takes effect after a restart.\n"
      << "gpu = " << Quote(settings.gpu) << "\n"
      << "# Its name, shown and logged only.\n"
      << "gpu_name = " << Quote(settings.gpu_name) << "\n"
      << "# Internal render resolution, \"WIDTHxHEIGHT\" or \"window\".\n"
      << "render_resolution = " << Quote(FormatResolution(settings.render_resolution)) << "\n"
      << "# Frame rate cap in Hz; 0 = unlimited.\n"
      << "fps_cap = " << settings.fps_cap << "\n"
      << "vsync = " << (settings.vsync ? "true" : "false") << "\n"
      << "# Game language (\"en\", \"de\", \"fr\", \"es\"...); takes effect after a restart.\n"
      << "language = " << Quote(settings.language) << "\n"
      << "# Aspect ratio of the game's frame: \"auto\" (the display's), \"4:3\", \"16:10\",\n"
      << "# \"16:9\", \"21:9\" or \"32:9\"; takes effect after a restart.\n"
      << "aspect = " << Quote(AspectName(settings.aspect)) << "\n"
      << "# Achievement set: \"xbox\" (the game's 12, the default) or \"pc\" (the PC version's,\n"
      << "# incomplete); takes effect after a restart.\n";
  if (settings.achievements) {
    out << "achievements = " << Quote(std::string(AchievementSetName(*settings.achievements)))
        << "\n";
  } else {
    out << "# achievements = \"xbox\"\n";
  }
  return out.str();
}

bool Load(const std::string& path, HostSettings& settings, std::vector<std::string>& warnings,
          std::string& error) {
  settings = HostSettings{};
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) {
    if (!ec) return true;  // no file yet: the defaults
    error = path + ": " + ec.message();
    return false;
  }
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    error = "cannot open " + path;
    return false;
  }
  std::ostringstream text;
  text << in.rdbuf();
  settings = Parse(text.str(), &warnings);
  return true;
}

bool Save(const std::string& path, const HostSettings& settings, std::string& error) {
  const std::string temp = path + ".tmp";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) {
      error = "cannot write " + temp;
      return false;
    }
    out << Serialize(settings);
    out.flush();
    if (!out) {
      error = "cannot write " + temp;
      return false;
    }
  }
  // On the disk before it replaces the old file, so a power cut leaves one or the other whole.
  if (!platform::FlushFileToDisk(temp, error) || !platform::CommitReplace(temp, path, error)) {
    std::error_code ec;
    std::filesystem::remove(temp, ec);
    return false;
  }
  return true;
}

}  // namespace torchlight::settings
