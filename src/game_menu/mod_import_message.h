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
// The busy notice while it runs (live/busy_notice_text.h fills {percent}).
inline constexpr const char* kImportBusy = "Importing mod... {percent}%";

// The box after ImportMod: what was installed, or why nothing was.
MenuMessage ModImportMessage(const mods::ImportResult& result, const Translate& tr);

// The box when nothing could be imported because the picker failed: no picker could be shown, or
// (Android) the chosen document could not be copied where it can be read. `detail`: the system's
// reason.
enum class PickProblem { kNoPicker, kNotCopied };
MenuMessage ModPickMessage(PickProblem problem, const std::string& detail, const Translate& tr);

// Every English text above and in the boxes, for the translations' check.
std::vector<std::string> ModImportTexts();

}  // namespace torchlight::game_menu
