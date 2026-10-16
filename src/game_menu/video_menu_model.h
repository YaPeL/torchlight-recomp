// What the video settings column in the game's settings menu shows and does, without the guest:
// each row's text, left/right through its choices, the vsync checkbox, and whether something that
// needs a restart changed. The rows' choices come from settings::Capabilities.

#pragma once

#include <string>

#include "settings/host_settings.h"

namespace torchlight::game_menu {

enum class Row {
  kResolution, kAspect, kFpsCap, kLanguage, kAchievements, kVsync, kRenderer, kGpu
};
inline constexpr int kRowCount = 8;

class VideoMenuModel {
 public:
  // `default_language`: the language the game runs in when the setting is empty (the runtime's,
  // which a --user_language can set).
  VideoMenuModel(settings::Capabilities caps, settings::HostSettings current,
                 settings::HostSettings startup, std::string default_language = "en");

  // Previous (direction < 0) or next choice of a value row, stopping at the ends like the menu's
  // sliders; true when the value changed. Disabled rows and the checkbox do not move.
  bool Step(Row row, int direction);
  // The vsync checkbox; true when it changed.
  bool Toggle(Row row);
  // Back to the default settings (the game menu's "Reset Defaults"); true when something changed.
  bool Reset();

  // The value shown in a row ("1920x1080", "Auto", "21:9", "Unlimited", "Español"...).
  std::string Text(Row row) const;
  bool Enabled(Row row) const;
  bool Checked(Row row) const;
  // A setting that takes effect after a restart differs from the one the game started with.
  bool RestartNeeded() const;

  const settings::HostSettings& settings() const { return current_; }

 private:
  std::string DefaultGpu() const;
  // The chosen render system is installed but cannot run here (Capabilities::
  // unavailable_render_systems).
  bool RendererUnavailable() const;
  std::string Language(const std::string& code) const {
    return code.empty() ? default_language_ : code;
  }

  settings::Capabilities caps_;
  settings::HostSettings current_, startup_;
  std::string default_language_;
};

// A GPU name short enough for a row: without words that do not tell GPUs apart ("GeForce",
// "Graphics", "Mobile", "Max-Q"...), and cut after the last whole word that fits in `max_chars`,
// with "...".
std::string ShortGpuName(const std::string& name, size_t max_chars = 20);

// The name of a language in that language ("English", "Deutsch", "Français", "Español"...), or
// the code when unknown.
std::string LanguageName(const std::string& code);

}  // namespace torchlight::game_menu
