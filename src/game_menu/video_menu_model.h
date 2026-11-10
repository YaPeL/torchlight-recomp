// What the video settings column in the game's settings menu shows and does, without the guest:
// each row's text, left/right through its choices, the vsync checkbox, the Import Mod action, and
// whether something that needs a restart changed. The rows' choices come from
// settings::Capabilities.

#pragma once

#include <string>

#include "settings/host_settings.h"

namespace torchlight::game_menu {

enum class Row {
  kResolution, kAspect, kFpsCap, kMsaa, kShadowResolution, kLanguage, kAchievements, kPadPrompts,
  kVsync, kSwapConfirmCancel, kRenderer, kGpu, kLowQuality, kImportMod
};
inline constexpr int kRowCount = 14;
// The rows shown as a checkbox (A toggles them) instead of a value moved with left and right.
inline bool IsCheckbox(Row row) { return row == Row::kVsync || row == Row::kSwapConfirmCancel; }
// The rows shown as a button that A presses (Activate), with no value: importing a mod.
inline bool IsAction(Row row) { return row == Row::kImportMod; }

// The column's sections, one shown at a time under their buttons: the video rows, the pad's
// (button prompts and the A/B swap) and the rest (language, achievements, Import Mod).
enum class Section { kVideo, kControls, kExtras };
inline constexpr int kSectionCount = 3;
Section SectionOf(Row row);

class VideoMenuModel {
 public:
  // `default_language`: the language the game runs in when the setting is empty (the runtime's,
  // which a --user_language can set).
  VideoMenuModel(settings::Capabilities caps, settings::HostSettings current,
                 settings::HostSettings startup, std::string default_language = "en");

  // Previous (direction < 0) or next choice of a value row, stopping at the ends like the menu's
  // sliders; true when the value changed. Disabled rows and the checkbox do not move.
  bool Step(Row row, int direction);
  // A checkbox row; true when it changed.
  bool Toggle(Row row);
  // An action row pressed: true when it may start now (the column then runs it).
  bool Activate(Row row) const { return IsAction(row) && Enabled(row); }
  // Back to the default settings (the game menu's "Reset Defaults"); true when something changed.
  bool Reset();

  // The value shown in a row ("1920x1080", "Auto", "21:9", "Unlimited", "Español"...).
  std::string Text(Row row) const;
  bool Enabled(Row row) const;
  // Whether the row is shown at all (in its section): Import Mod not in a gamescope session (the
  // owner, 2026-11-09: hidden in the Deck's Game Mode, where the desktop's picker can open unseen).
  bool Shown(Row row) const;
  bool Checked(Row row) const;
  // A setting that takes effect after a restart differs from the one the game started with, or a
  // mod was imported (the game loads mods when it starts).
  bool RestartNeeded() const;

  const settings::HostSettings& settings() const { return current_; }
  // The device's most multisampling once the backend reports it (Capabilities::max_msaa; the
  // column may be built before the backend started).
  void SetMaxMsaa(uint32_t max_msaa) { caps_.max_msaa = max_msaa; }
  // Whether the pad connected now is a Nintendo one: the automatic style follows it, so the A/B
  // swap applies with it only (mods::EffectivePadStyle).
  void SetNintendoPadConnected(bool connected) { nintendo_pad_ = connected; }
  // Import Mod: offered where the system has a file picker and the mods folder is known; disabled
  // while an import runs; a mod installed needs a restart.
  void SetImportAvailable(bool available) { import_available_ = available; }
  void SetImportHidden(bool hidden) { import_hidden_ = hidden; }
  void SetImportRunning(bool running) { import_running_ = running; }
  void NoteModImported() { mod_imported_ = true; }

 private:
  std::string DefaultGpu() const;
  // The chosen render system is installed but cannot run here (Capabilities::
  // unavailable_render_systems).
  bool RendererUnavailable() const;
  std::string Language(const std::string& code) const {
    return code.empty() ? default_language_ : code;
  }

  settings::Capabilities caps_;

  bool nintendo_pad_ = false;
  bool import_available_ = false, import_running_ = false, mod_imported_ = false, import_hidden_ = false;
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
