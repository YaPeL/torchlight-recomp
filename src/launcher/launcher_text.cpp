#include "launcher/launcher_text.h"

#include <array>

#include "launcher/file_browser_model.h"

namespace torchlight::launcher {

std::span<const std::string_view> LauncherTexts() {
  static constexpr std::array kTexts = {
      kTextInstallHeading,   kTextInstallingHeading,  kTextAchievementsHeading,
      kTextReadyHeading,     kTextPlay,               kTextBack,
      kTextCancel,           kTextReinstall,          kTextChangeAchievements,
      kTextCancelling,       kTextReady,              kTextAchievementSet,
      kTextParentFolder,     kTextUseFolder,          kTextEmptyFolder,
      FileBrowserModel::kTextCannotOpen,
  };
  return kTexts;
}

}  // namespace torchlight::launcher
