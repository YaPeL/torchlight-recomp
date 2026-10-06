#include "game_menu/video_menu_model.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace torchlight::game_menu {

namespace {

template <typename T>
size_t IndexOf(const std::vector<T>& values, const T& value) {
  auto it = std::find(values.begin(), values.end(), value);
  return it == values.end() ? 0 : size_t(it - values.begin());
}

// One step through `values` from `value`, clamped at the ends.
template <typename T>
bool StepIn(const std::vector<T>& values, T& value, int direction) {
  if (values.size() < 2) return false;
  const size_t index = IndexOf(values, value);
  const size_t next = direction < 0 ? (index == 0 ? 0 : index - 1)
                                    : std::min(index + 1, values.size() - 1);
  if (values[next] == value) return false;
  value = values[next];
  return true;
}

}  // namespace

std::string ShortGpuName(const std::string& name, size_t max_chars) {
  static const char* const kNoise[] = {"GeForce", "Graphics", "Mobile", "Max-Q", "Design",
                                       "with",    "(R)",      "(TM)",   "Corporation"};
  std::vector<std::string> words;
  size_t start = 0;
  while (start < name.size()) {
    size_t end = name.find(' ', start);
    if (end == std::string::npos) end = name.size();
    const std::string word = name.substr(start, end - start);
    bool noise = word.empty();
    for (const char* n : kNoise) noise = noise || word == n;
    if (!noise) words.push_back(word);
    start = end + 1;
  }
  if (words.empty()) return name;
  // Whole words while they fit, with "..." when some are left out.
  std::string out = words[0];
  for (size_t i = 1; i < words.size(); ++i) {
    if (out.size() + 1 + words[i].size() > max_chars) return out + "...";
    out += " " + words[i];
  }
  return out;
}

std::string LanguageName(const std::string& code) {
  // In their own language for Latin scripts; in English otherwise, since the game's fonts may
  // have no glyphs for them until the language's own font is loaded.
  static const std::pair<const char*, const char*> kNames[] = {
      {"en", "English"},      {"de", "Deutsch"},    {"fr", "Français"},
      {"es", "Español"},      {"it", "Italiano"},   {"pt", "Português"},
      {"pt-br", "Português (Brasil)"}, {"pl", "Polski"}, {"ru", "Russian"},
      {"uk", "Ukrainian"},    {"ja", "Japanese"},   {"ko", "Korean"},
      {"zh-cn", "Chinese (Simplified)"}, {"zh-tw", "Chinese (Traditional)"},
  };
  for (const auto& [c, name] : kNames) {
    if (code == c) return name;
  }
  return code;
}

VideoMenuModel::VideoMenuModel(settings::Capabilities caps, settings::HostSettings current,
                               settings::HostSettings startup, std::string default_language)
    : caps_(std::move(caps)),
      current_(std::move(current)),
      startup_(std::move(startup)),
      default_language_(default_language.empty() ? "en" : std::move(default_language)) {}

bool VideoMenuModel::Step(Row row, int direction) {
  if (!Enabled(row)) return false;
  switch (row) {
    case Row::kResolution: {
      // "window" (0x0) is the display's size, the first choice; choosing that size again stores
      // "window", so the default keeps following the display.
      const auto values = settings::ResolutionChoices(caps_).values;
      settings::Resolution shown =
          current_.render_resolution == settings::Resolution{} ? caps_.display
                                                               : current_.render_resolution;
      if (!StepIn(values, shown, direction)) return false;
      current_.render_resolution = shown == caps_.display ? settings::Resolution{} : shown;
      return true;
    }
    case Row::kAspect:
      return StepIn(settings::AspectChoices(caps_).values, current_.aspect, direction);
    case Row::kFpsCap:
      return StepIn(settings::FpsCapChoices(caps_).values, current_.fps_cap, direction);
    case Row::kAchievements: {
      // An explicit choice from here on, also when it was the default.
      settings::AchievementSet shown = settings::EffectiveAchievements(current_);
      if (!StepIn(std::vector<settings::AchievementSet>{settings::AchievementSet::kXbox,
                                                        settings::AchievementSet::kPc},
                  shown, direction)) {
        return false;
      }
      current_.achievements = shown;
      return true;
    }
    case Row::kLanguage: {
      // Empty is the language the game started with by default; choosing it again stores empty.
      std::string shown = Language(current_.language);
      if (!StepIn(settings::LanguageChoices(caps_).values, shown, direction)) return false;
      current_.language = shown == default_language_ ? std::string() : shown;
      return true;
    }
    case Row::kRenderer: {
      const auto values = settings::RenderSystemChoices(caps_).values;
      // From a saved choice this machine cannot run (the session uses another), either way goes to
      // the first one that can: changing it is always the player's own choice.
      if (RendererUnavailable()) {
        if (values.empty()) return false;
        current_.render_system = values.front();
        return true;
      }
      return StepIn(values, current_.render_system, direction);
    }
    case Row::kGpu: {
      // Empty is the default GPU; choosing it again stores empty (as "window" for resolution).
      const std::string default_id = DefaultGpu();
      std::string shown = current_.gpu.empty() ? default_id : current_.gpu;
      if (!StepIn(settings::GpuChoices(caps_).values, shown, direction)) return false;
      current_.gpu = shown == default_id ? std::string() : shown;
      current_.gpu_name.clear();
      for (const auto& gpu : caps_.gpus) {
        if (!current_.gpu.empty() && gpu.id == current_.gpu) current_.gpu_name = gpu.name;
      }
      return true;
    }
    case Row::kVsync:
      return false;
  }
  return false;
}

bool VideoMenuModel::Toggle(Row row) {
  if (row != Row::kVsync) return false;
  current_.vsync = !current_.vsync;
  return true;
}

bool VideoMenuModel::Reset() {
  const settings::HostSettings defaults = settings::Normalize(settings::HostSettings{}, caps_);
  if (defaults == current_) return false;
  current_ = defaults;
  return true;
}

std::string VideoMenuModel::Text(Row row) const {
  switch (row) {
    case Row::kResolution: {
      const settings::Resolution r =
          current_.render_resolution == settings::Resolution{} ? caps_.display
                                                               : current_.render_resolution;
      if (!r.width || !r.height) return "Window";
      return std::to_string(r.width) + "x" + std::to_string(r.height);
    }
    case Row::kAspect:
      return current_.aspect == settings::Aspect::kAuto
                 ? "Auto"
                 : std::string(settings::AspectName(current_.aspect));
    case Row::kFpsCap:
      return current_.fps_cap ? std::to_string(current_.fps_cap) : "Unlimited";
    case Row::kAchievements:
      return settings::EffectiveAchievements(current_) == settings::AchievementSet::kPc
                 ? "PC (incomplete)"
                 : "Xbox 360";
    case Row::kLanguage:
      return LanguageName(Language(current_.language));
    case Row::kRenderer:
    {
      // OGRE's names ("OpenGL 3+ Rendering Subsystem") without the suffix; automatic shows the
      // first one offered.
      std::string name = current_.render_system;
      if (name.empty() && !caps_.render_systems.empty()) name = caps_.render_systems.front();
      if (auto at = name.find(" Rendering Subsystem"); at != std::string::npos) name.resize(at);
      // A saved choice this machine cannot run: shown as it is, marked (the session uses another).
      if (RendererUnavailable()) name += " (unavailable)";
      return name;
    }
    case Row::kGpu: {
      const std::string id = current_.gpu.empty() ? DefaultGpu() : current_.gpu;
      for (const auto& gpu : caps_.gpus) {
        if (gpu.id == id) return ShortGpuName(gpu.name);
      }
      // GPUs but none the default (Windows): the system decides.
      return id.empty() && !caps_.gpus.empty() ? "Auto" : "Default";
    }
    case Row::kVsync:
      return current_.vsync ? "On" : "Off";
  }
  return "";
}

bool VideoMenuModel::Enabled(Row row) const {
  switch (row) {
    case Row::kResolution: return settings::ResolutionChoices(caps_).enabled;
    case Row::kAspect: return settings::AspectChoices(caps_).enabled;
    case Row::kFpsCap: return settings::FpsCapChoices(caps_).enabled;
    case Row::kAchievements: return true;
    case Row::kLanguage: return settings::LanguageChoices(caps_).enabled;
    case Row::kRenderer:
      return settings::RenderSystemChoices(caps_).enabled ||
             (RendererUnavailable() && !settings::RenderSystemChoices(caps_).values.empty());
    case Row::kGpu:
      return settings::GpuChoices(caps_).enabled &&
             settings::GpuChoiceApplies(caps_, current_.render_system);
    case Row::kVsync: return true;
  }
  return false;
}

std::string VideoMenuModel::DefaultGpu() const {
  for (const auto& gpu : caps_.gpus) {
    if (gpu.is_default) return gpu.id;
  }
  return "";
}

bool VideoMenuModel::RendererUnavailable() const {
  return !current_.render_system.empty() &&
         std::find(caps_.unavailable_render_systems.begin(), caps_.unavailable_render_systems.end(),
                   current_.render_system) != caps_.unavailable_render_systems.end();
}

bool VideoMenuModel::Checked(Row row) const { return row == Row::kVsync && current_.vsync; }

bool VideoMenuModel::RestartNeeded() const {
  return Language(current_.language) != Language(startup_.language) ||
         current_.render_system != startup_.render_system || current_.gpu != startup_.gpu ||
         current_.aspect != startup_.aspect ||
         settings::EffectiveAchievements(current_) != settings::EffectiveAchievements(startup_);
}

}  // namespace torchlight::game_menu
