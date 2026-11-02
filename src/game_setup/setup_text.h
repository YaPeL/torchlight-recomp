// The first start's texts (first_run.h): English in the code, as templates with {placeholders},
// translated from data/ui/tl_setup_strings.txt (game_menu::MenuStrings' format: a section per
// language code, "English text = translation"). A new text needs its line in every section;
// setup_text_test checks it. Texts written for this project, in the register of the game's own
// translations (de: du, fr: vous, es: tú).

#pragma once

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace torchlight::game_setup {

// An English template and the values of its {placeholders}; an empty text means no message.
struct Message {
  std::string text;
  std::vector<std::pair<std::string, std::string>> values;
  bool empty() const { return text.empty(); }
};

using Translate = std::function<std::string(const std::string& english)>;

// The message translated (no translation: the English text) with its placeholders filled.
std::string Render(const Message& message, const Translate& translate = {});

// The language for the first start's texts: the host settings' language when set; else the console
// language given with --user_language (its code); else the first of the system's preferred
// languages that has a section in the strings (`available`); else English.
std::string SetupLanguage(const std::string& settings_language,
                          const std::optional<std::string>& console_language,
                          const std::vector<std::string>& preferred,
                          const std::vector<std::string>& available);

// The interface's own texts (the message box, buttons, picker titles, progress lines), besides the
// messages game_files.h and install.h return: listed for the translation check.
inline constexpr std::string_view kTextTitle = "Torchlight setup";
// The renderer could not start (ShowRendererError): the window would stay black.
inline constexpr std::string_view kTextRendererTitle = "Torchlight Recomp";
inline constexpr std::string_view kTextRendererFailed =
    "The game could not start its renderer: {error}\n\nIt needs OpenGL 3.3, or Direct3D 11 on "
    "Windows. Update the graphics driver, and on a computer with two GPUs make sure the game runs on "
    "the one with the driver installed.";
inline constexpr std::string_view kTextIntro =
    "Torchlight needs the game files from your copy of Torchlight for Xbox Live Arcade (version "
    "1.0.140.0).\n\nChoose the game's package (the file copied from your console, which starts "
    "with LIVE) or a folder where it is already extracted. The files are checked and copied "
    "to:\n{where}";
// Where no folder can be picked (Android: platform::CanPickFolder).
inline constexpr std::string_view kTextIntroPackageOnly =
    "Torchlight needs the game files from your copy of Torchlight for Xbox Live Arcade (version "
    "1.0.140.0).\n\nChoose the game's package (the file copied from your console, which starts "
    "with LIVE). The files are checked and copied to:\n{where}";
inline constexpr std::string_view kTextChoosePackage = "Choose package...";
inline constexpr std::string_view kTextChooseFolder = "Choose folder...";
inline constexpr std::string_view kTextQuit = "Quit";
inline constexpr std::string_view kTextPackagePicker = "Torchlight package (Xbox Live Arcade)";
inline constexpr std::string_view kTextFolderPicker = "Folder with the extracted game files";
// The chosen file could not be copied to where it can be read (Android: PickResult::kUncopied).
inline constexpr std::string_view kTextPickedCopyFailed =
    "The chosen file could not be copied ({error}).\n\nThe first start needs free space for the "
    "package and, after it, for the game's files.";
inline constexpr std::string_view kTextPickerFailed =
    "The file chooser could not be opened ({error}).\n\nOn the Steam Deck, do this first setup in "
    "Desktop Mode.";
inline constexpr std::string_view kTextExtracting = "Extracting the game files...";
inline constexpr std::string_view kTextCopying = "Copying the game files...";
inline constexpr std::string_view kTextChecking = "Checking the game files...";
// The achievement set's page (EnsureAchievementChoice; docs/achievements-xbox.md).
inline constexpr std::string_view kTextAchievementsChoice =
    "Choose which achievements to earn. Xbox 360: the game's original 12 achievements. PC: the 66 "
    "achievements of the PC version. Each set keeps its own progress. You can change "
    "this later in the settings (restart required).";
inline constexpr std::string_view kTextAchievementsXbox = "Xbox 360";
inline constexpr std::string_view kTextAchievementsPc = "PC";
// The wizard's pages (setup_pages.h).
inline constexpr std::string_view kTextPageGame = "The game's files";
inline constexpr std::string_view kTextPageWorking = "Installing the game's files";
inline constexpr std::string_view kTextPageAchievements = "Achievements";
inline constexpr std::string_view kTextPageReady = "Ready";
inline constexpr std::string_view kTextReady = "Everything is set. The game starts now.";
inline constexpr std::string_view kTextStart = "Start the game";
inline constexpr std::string_view kTextBrowsePad = "Browse with the pad...";
inline constexpr std::string_view kTextBrowseTitle = "Choose the package, or a folder extracted from it";
inline constexpr std::string_view kTextCancel = "Cancel";
inline constexpr std::string_view kTextBack = "Back";
inline constexpr std::string_view kTextHint = "A: choose     B: back     D-pad or arrows: move";
inline constexpr std::string_view kTextHintNext =
    "A: choose     B: back     Start: next     D-pad or arrows: move";
inline constexpr std::string_view kTextFolderAbove = "..  (the folder above)";
inline constexpr std::string_view kTextThisComputer = "This computer";
inline constexpr std::string_view kTextUseFolder = "Use this folder";
inline constexpr std::string_view kTextFolderMark = "Folder:";
std::span<const std::string_view> InterfaceTexts();

}  // namespace torchlight::game_setup
