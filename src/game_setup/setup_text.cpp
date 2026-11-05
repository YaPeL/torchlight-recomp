#include "game_setup/setup_text.h"

#include <algorithm>

namespace torchlight::game_setup {

namespace {
constexpr std::string_view kInterfaceTexts[] = {
    kTextTitle,         kTextIntro,          kTextIntroPackageOnly, kTextChoosePackage,
    kTextChooseFolder,
    kTextQuit,          kTextPackagePicker,  kTextFolderPicker,  kTextPickerFailed,
    kTextPickedCopyFailed,
    kTextExtracting,    kTextCopying,        kTextChecking,      kTextAchievementsChoice,
    kTextAchievementsXbox, kTextAchievementsPc,
    kTextPageGame, kTextPageWorking, kTextPageAchievements, kTextPageReady, kTextReady, kTextStart,
    kTextReadyDeck, kTextClose,
    kTextBrowsePad, kTextBrowseTitle, kTextCancel, kTextBack, kTextHint, kTextHintNext,
    kTextFolderAbove, kTextThisComputer, kTextUseFolder, kTextFolderMark, kTextNoWindow,
    kTextPageStorage, kTextStorageIntro, kTextKeepHere, kTextCopyUser, kTextCopyUserNote, kTextCopyPortable, kTextUseUser, kTextParentUnwritableTitle, kTextParentUnwritable, kTextPortableUnwritableTitle, kTextPortableUnwritable, kTextNotPortable, kTextPlaceReasonLinux, kTextPlaceReasonWindows, kTextPlaceReasonMac, kTextFolderReasonLinux, kTextFolderReasonWindows, kTextFolderReasonMac, kTextElevatedTitle, kTextElevated, kTextPortableSource, kTextPageCopying, kTextCannotCopyData,
};
}  // namespace

std::span<const std::string_view> InterfaceTexts() { return kInterfaceTexts; }

std::string_view UnwritableReason(platform::OsKind os, bool existing) {
  switch (os) {
    case platform::OsKind::kWindows: return existing ? kTextFolderReasonWindows : kTextPlaceReasonWindows;
    case platform::OsKind::kMacOS: return existing ? kTextFolderReasonMac : kTextPlaceReasonMac;
    case platform::OsKind::kLinux:
    case platform::OsKind::kAndroid: break;
  }
  return existing ? kTextFolderReasonLinux : kTextPlaceReasonLinux;
}

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
