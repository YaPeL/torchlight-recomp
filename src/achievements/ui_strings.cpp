#include "achievements/ui_strings.h"

#include <filesystem>
#include <mutex>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "game_menu/menu_strings.h"
#include "live/install.h"
#include "platform/platform.h"
#include "settings/host_settings.h"

namespace torchlight::achievements {
namespace {
constexpr const char* kStringsFile = "tl_achievement_strings.txt";
std::once_flag g_loaded;
game_menu::MenuStrings g_strings;
std::string g_language = "en";

void Load() {
  const std::filesystem::path file =
      std::filesystem::path(platform::ExecutableDir()) / "data" / "ui" / kStringsFile;
  std::vector<std::string> warnings;
  std::string error;
  if (!g_strings.Load(file.string(), warnings, error)) {
    REXLOG_WARN("achievement texts: {}; texts in English", error);
  }
  for (const std::string& warning : warnings) REXLOG_WARN("achievement texts: {}: {}", kStringsFile, warning);
  // As the video menu picks its language: a language pack, else the console language.
  const std::string pack = live::LanguagePack();
  g_language = pack.empty() ? settings::LanguageCode(rex::cvar::Query<uint32_t>("user_language")) : pack;
}
}  // namespace

std::string Translate(std::string_view english) {
  std::call_once(g_loaded, Load);
  return g_strings.Translate(g_language, std::string(english));
}
}  // namespace torchlight::achievements
