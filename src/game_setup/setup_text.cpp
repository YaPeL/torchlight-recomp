#include "game_setup/setup_text.h"

#include <algorithm>

namespace torchlight::game_setup {

namespace {
constexpr std::string_view kInterfaceTexts[] = {
    kTextTitle,         kTextIntro,          kTextChoosePackage, kTextChooseFolder,
    kTextQuit,          kTextPackagePicker,  kTextFolderPicker,  kTextPickerFailed,
    kTextExtracting,    kTextCopying,        kTextChecking,      kTextAchievementsChoice,
    kTextAchievementsXbox, kTextAchievementsPc,
};
}  // namespace

std::span<const std::string_view> InterfaceTexts() { return kInterfaceTexts; }

std::string Render(const Message& message, const Translate& translate) {
  std::string text = translate ? translate(message.text) : message.text;
  for (const auto& [name, value] : message.values) {
    const std::string placeholder = "{" + name + "}";
    for (size_t at = text.find(placeholder); at != std::string::npos;
         at = text.find(placeholder, at + value.size())) {
      text.replace(at, placeholder.size(), value);
    }
  }
  return text;
}

std::string SetupLanguage(const std::string& settings_language,
                          const std::optional<std::string>& console_language,
                          const std::vector<std::string>& preferred,
                          const std::vector<std::string>& available) {
  if (!settings_language.empty()) return settings_language;
  if (console_language) return *console_language;
  for (const std::string& language : preferred) {
    if (std::find(available.begin(), available.end(), language) != available.end()) {
      return language;
    }
  }
  return "en";
}

}  // namespace torchlight::game_setup
