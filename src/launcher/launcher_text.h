// The launcher's own texts, besides the first start's (game_setup/setup_text.h) it reuses: English
// templates, translated through the same table (data/ui/tl_setup_strings.txt; docs/launcher.md,
// stage 1, step 4).

#pragma once

#include <span>
#include <string_view>

namespace torchlight::launcher {

// The project's name, shown as it is (not translated).
inline constexpr std::string_view kProjectName = "Torchlight Recomp";

// Page headings.
inline constexpr std::string_view kTextInstallHeading = "Install the game files";
inline constexpr std::string_view kTextInstallingHeading = "Installing";
inline constexpr std::string_view kTextAchievementsHeading = "Achievements";
inline constexpr std::string_view kTextReadyHeading = "Ready to play";
// Buttons.
inline constexpr std::string_view kTextPlay = "Play";
inline constexpr std::string_view kTextBack = "Back";
inline constexpr std::string_view kTextCancel = "Cancel";
inline constexpr std::string_view kTextReinstall = "Install the game files again...";
inline constexpr std::string_view kTextChangeAchievements = "Change achievements...";
// Installing.
inline constexpr std::string_view kTextCancelling = "Cancelling...";
// Ready.
inline constexpr std::string_view kTextReady = "The game files are installed in:\n{where}";
inline constexpr std::string_view kTextAchievementSet = "Achievements: {set}";
// The file browser.
inline constexpr std::string_view kTextParentFolder = "Parent folder";
inline constexpr std::string_view kTextUseFolder = "Use this folder";
inline constexpr std::string_view kTextEmptyFolder = "This folder is empty.";

// The texts above but the project's name, and the file browser's error (file_browser_model.h), for
// the translation check (setup_text_test).
std::span<const std::string_view> LauncherTexts();

}  // namespace torchlight::launcher
