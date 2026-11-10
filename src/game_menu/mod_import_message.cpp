#include "game_menu/mod_import_message.h"

#include <algorithm>
#include <utility>

namespace torchlight::game_menu {

namespace {

constexpr const char* kInstalled =
    "Installed: {mods}. Mods are loaded when the game starts: restart it to play with them.";
// More than kListedMods: the first ones named, the rest counted.
constexpr const char* kInstalledMany =
    "Installed {count} mods: {mods} and {more} more. Mods are loaded when the game starts: restart "
    "it to play with them.";
constexpr const char* kAlreadyInstalled =
    "A mod named {mods} is already installed, so nothing was changed. Remove or rename it in the "
    "mods folder to import this one.";
constexpr const char* kNoModFound =
    "No mod was found. A mod has a mod.dat file or a media folder, at the top of the zip or folder "
    "or in a folder inside it.";
constexpr const char* kUnreadable = "The zip or folder could not be read: {detail}";
constexpr const char* kUnsafeName =
    "The mod was not imported: a file name in it could write outside its folder ({detail}).";
constexpr const char* kUnsupportedEntry =
    "The mod was not imported: it holds a link, an encrypted file or a compression this import "
    "cannot read ({detail}).";
constexpr const char* kNonAsciiName =
    "The mod was not imported: its zip writes file names in an old Windows encoding ({detail}), "
    "which this import cannot read. Unzip it and zip it again with a recent tool (7-Zip, or the "
    "system's own), which writes the names as UTF-8.";
constexpr const char* kNotAZip = "{detail} is not a zip file. Choose the mod's .zip file.";
constexpr const char* kReplaceQuestion =
    "A mod named {mods} is already installed. Replace it with the one you chose?";
constexpr const char* kReplaceButton = "Replace";
constexpr const char* kReplaced = "Replaced: {mods}. Restart the game to use the new version.";
constexpr const char* kReplacedInPlay =
    "Replaced: {mods}. Restart the game from the title screen to use the new version.";
constexpr const char* kQuitNowButton = "Quit Now";
constexpr const char* kLaterButton = "Later";
constexpr const char* kReplaceKept =
    "The new version could not be installed ({detail}). The installed one was kept as it was: close "
    "the game and import it again.";
constexpr const char* kNotEnoughSpace =
    "There is not enough free space for the mod: it needs {needed} MB and {free} MB are free.";
constexpr const char* kCannotWrite = "The mod could not be written to the mods folder: {detail}";
constexpr const char* kCancelled = "The import was cancelled; nothing was installed.";
constexpr const char* kNoPicker = "No file picker could be opened: {detail}";
constexpr const char* kNotCopied = "The chosen file could not be copied: {detail}";

// Translates `english` and fills its {placeholders}.
std::string Text(const Translate& tr, const std::string& english,
                 const std::vector<std::pair<std::string, std::string>>& values = {}) {
  // One pass over the translated text, so a value holding "{detail}" (a mod's name) stays as it is.
  const std::string translated = tr(english);
  std::string text;
  size_t at = 0;
  while (at < translated.size()) {
    bool filled = false;
    if (translated[at] == '{') {
      for (const auto& [key, value] : values) {
        const std::string placeholder = "{" + key + "}";
        if (translated.compare(at, placeholder.size(), placeholder) == 0) {
          text += value;
          at += placeholder.size();
          filled = true;
          break;
        }
      }
    }
    if (!filled) text += translated[at++];
  }
  return text;
}

std::string Megabytes(uint64_t bytes) { return std::to_string((bytes + (1 << 20) - 1) >> 20); }

MenuMessage Box(const Translate& tr, const std::string& text) {
  return {Text(tr, kImportTitle), text, {Text(tr, kImportOk)}};
}

}  // namespace

std::string WrapText(const std::string& text, size_t width) {
  std::string out;
  size_t line = 0;  // the current line's length so far
  size_t at = 0;
  while (at < text.size()) {
    if (text[at] == '\n') {
      out += '\n';
      line = 0;
      ++at;
      continue;
    }
    if (text[at] == ' ') {
      ++at;
      continue;
    }
    size_t end = text.find_first_of(" \n", at);
    if (end == std::string::npos) end = text.size();
    const std::string word = text.substr(at, end - at);
    // Characters, not bytes: UTF-8 continuation bytes do not count.
    const size_t length = static_cast<size_t>(
        std::count_if(word.begin(), word.end(), [](char c) { return (c & 0xC0) != 0x80; }));
    if (line > 0 && line + 1 + length > width) {
      out += '\n';
      line = 0;
    } else if (line > 0) {
      out += ' ';
      ++line;
    }
    out += word;
    line += length;
    at = end;
  }
  return out;
}

MenuMessage ModImportMessage(const mods::ImportResult& result, const Translate& tr, bool in_play) {
  const size_t listed = std::min(result.mods.size(), kListedMods);
  std::string names;
  for (size_t i = 0; i < listed; ++i) names += (names.empty() ? "" : ", ") + result.mods[i];
  const bool many = result.mods.size() > kListedMods;
  if (many && result.status != mods::ImportStatus::kInstalled) names += ", ...";
  const std::vector<std::pair<std::string, std::string>> values = {
      {"mods", names},
      {"count", std::to_string(result.mods.size())},
      {"more", std::to_string(result.mods.size() - listed)},
      {"detail", result.detail},
      {"needed", Megabytes(result.bytes_needed)},
      {"free", Megabytes(result.bytes_free)}};
  using mods::ImportStatus;
  const char* english = kCannotWrite;
  switch (result.status) {
    case ImportStatus::kInstalled: english = many ? kInstalledMany : kInstalled; break;
    case ImportStatus::kCancelled: english = kCancelled; break;
    case ImportStatus::kNoModFound: english = kNoModFound; break;
    case ImportStatus::kAlreadyInstalled: english = kAlreadyInstalled; break;
    case ImportStatus::kUnreadable: english = kUnreadable; break;
    case ImportStatus::kUnsafeName: english = kUnsafeName; break;
    case ImportStatus::kUnsupportedEntry: english = kUnsupportedEntry; break;
    case ImportStatus::kNonAsciiName: english = kNonAsciiName; break;
    case ImportStatus::kNotAZip: english = kNotAZip; break;
    case ImportStatus::kNotEnoughSpace: english = kNotEnoughSpace; break;
    case ImportStatus::kCannotWrite: english = kCannotWrite; break;
  }
  if (result.ok() && !result.replaced.empty() && in_play) return Box(tr, Text(tr, kReplacedInPlay, values));
  if (result.ok() && !result.replaced.empty()) {
    return {Text(tr, kImportTitle), Text(tr, kReplaced, values), {Text(tr, kQuitNowButton), Text(tr, kLaterButton)},
            1, 0};
  }
  if (!result.ok() && result.old_mods_kept && result.status == ImportStatus::kCannotWrite) english = kReplaceKept;
  return Box(tr, Text(tr, english, values));
}

MenuMessage ModReplaceQuestion(const mods::ImportResult& result, const Translate& tr) {
  std::string names;
  for (size_t i = 0; i < std::min(result.mods.size(), kListedMods); ++i) {
    names += (names.empty() ? "" : ", ") + result.mods[i];
  }
  if (result.mods.size() > kListedMods) names += ", ...";
  return {Text(tr, kImportTitle), Text(tr, kReplaceQuestion, {{"mods", names}}),
          {Text(tr, kReplaceButton), Text(tr, kImportCancelButton)},
          1};
}

MenuMessage ModPickMessage(PickProblem problem, const std::string& detail, const Translate& tr) {
  return Box(tr, Text(tr, problem == PickProblem::kNoPicker ? kNoPicker : kNotCopied,
                      {{"detail", detail}}));
}

std::vector<std::string> ModImportTexts() {
  return {kImportTitle,       kImportOk,          kImportAskSource, kImportZipButton,
          kImportFolderButton, kImportCancelButton, kImportPickZip,   kImportPickFolder,
          kImportZipFilter,   kImportAllFilter,   kImportCopying,     kImportBusy,      kInstalled,
          kInstalledMany,     kAlreadyInstalled,
          kNoModFound,        kUnreadable,        kUnsafeName,      kUnsupportedEntry,
          kNonAsciiName,      kNotAZip,           kReplaceQuestion, kReplaceButton,
          kReplaced,          kReplacedInPlay,    kQuitNowButton,     kLaterButton,     kReplaceKept,
          kNotEnoughSpace,    kCannotWrite,     kCancelled,
          kNoPicker,          kNotCopied};
}

}  // namespace torchlight::game_menu
