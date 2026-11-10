// The texts around Import Mod in the settings column (video_menu.cpp): the box that reports an
// import's result (mods/mod_import.h) or why no source could be picked, the desktop's question
// between a zip and a folder, the pickers' titles and the busy notice. English here, translated
// with `tr` (data/ui/tl_video_strings.txt); {words} in braces are filled in after translating.

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "mods/mod_import.h"

namespace torchlight::game_menu {

using Translate = std::function<std::string(const std::string&)>;

struct MenuMessage {
  std::string title, text;
  std::vector<std::string> buttons;
  int active = 0;  // the button selected when the box opens
};

inline constexpr const char* kImportTitle = "Import Mod";
inline constexpr const char* kImportOk = "OK";
// The desktop's question, where both pickers work (platform::CanPickFolder): its buttons in order.
inline constexpr const char* kImportAskSource = "Import a mod from a .zip file or from a folder?";
inline constexpr const char* kImportZipButton = "Zip File";
inline constexpr const char* kImportFolderButton = "Folder";
inline constexpr const char* kImportCancelButton = "Cancel";
inline constexpr const char* kImportPickZip = "Choose the mod's .zip file";
inline constexpr const char* kImportPickFolder = "Choose the mod's folder";
inline constexpr const char* kImportZipFilter = "Zip archives";
// Every file too (the owner, 2026-11-09): a provider may label a zip otherwise, and the import
// tells a file that is not a zip apart. On Android both filters together offer every file.
inline constexpr const char* kImportAllFilter = "All files";
// The busy notices while the chosen file is copied (Android: the picker's document) and while it
// is imported (live/busy_notice_text.h fills {percent}).
inline constexpr const char* kImportCopying = "Copying mod... {percent}%";
inline constexpr const char* kImportBusy = "Importing mod... {percent}%";

// The boxes' text is wrapped at this many characters when shown (WrapText): the runtime's message
// box draws each line whole, so a long one ran off the screen (29 mods' names on the POCO,
// 2026-11-09).
inline constexpr size_t kBoxLineLength = 70;
// The mods named in a box; the rest are counted.
inline constexpr size_t kListedMods = 5;
// Breaks `text` into lines of at most `width` characters at spaces (a longer word gets a line of
// its own), keeping its own line breaks.
std::string WrapText(const std::string& text, size_t width);

// The box after ImportMod: what was installed, or why nothing was.
MenuMessage ModImportMessage(const mods::ImportResult& result, const Translate& tr);

// The question after an import refused for a name already installed (kAlreadyInstalled): Replace
// (button 0, imported again with mods::ImportOptions::replace) or Cancel (button 1, selected when
// it opens, so that pressing A through it does not replace a mod).
MenuMessage ModReplaceQuestion(const mods::ImportResult& result, const Translate& tr);

// The box when nothing could be imported because the picker failed: no picker could be shown, or
// (Android) the chosen document could not be copied where it can be read. `detail`: the system's
// reason.
enum class PickProblem { kNoPicker, kNotCopied };
MenuMessage ModPickMessage(PickProblem problem, const std::string& detail, const Translate& tr);

// Every English text above and in the boxes, for the translations' check.
std::vector<std::string> ModImportTexts();

}  // namespace torchlight::game_menu
