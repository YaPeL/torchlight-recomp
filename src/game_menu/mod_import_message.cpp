#include "game_menu/mod_import_message.h"

#include <utility>

namespace torchlight::game_menu {

namespace {

constexpr const char* kInstalled =
    "Installed: {mods}. Mods are loaded when the game starts: restart it to play with them.";
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
    "The mod was not imported: the zip does not say how its file names are written ({detail}). "
    "Zip it again with a tool that saves the names as UTF-8.";
constexpr const char* kNotEnoughSpace =
    "There is not enough free space for the mod: it needs {needed} MB and {free} MB are free.";
constexpr const char* kCannotWrite = "The mod could not be written to the mods folder: {detail}";
constexpr const char* kCancelled = "The import was cancelled; nothing was installed.";
constexpr const char* kNoPicker = "No file picker could be opened: {detail}";
constexpr const char* kNotCopied = "The chosen file could not be copied: {detail}";

// Translates `english` and fills its {placeholders}.
std::string Text(const Translate& tr, const std::string& english,
                 const std::vector<std::pair<std::string, std::string>>& values = {}) {
  std::string text = tr(english);
  for (const auto& [key, value] : values) {
    const std::string placeholder = "{" + key + "}";
    for (size_t at = text.find(placeholder); at != std::string::npos;
         at = text.find(placeholder, at + value.size())) {
      text.replace(at, placeholder.size(), value);
    }
  }
  return text;
}

std::string Megabytes(uint64_t bytes) { return std::to_string((bytes + (1 << 20) - 1) >> 20); }

MenuMessage Box(const Translate& tr, std::string text) {
  return {Text(tr, kImportTitle), std::move(text), {Text(tr, kImportOk)}};
}

}  // namespace

MenuMessage ModImportMessage(const mods::ImportResult& result, const Translate& tr) {
  std::string names;
  for (const std::string& mod : result.mods) names += (names.empty() ? "" : ", ") + mod;
  const std::vector<std::pair<std::string, std::string>> values = {
      {"mods", names},
      {"detail", result.detail},
      {"needed", Megabytes(result.bytes_needed)},
      {"free", Megabytes(result.bytes_free)}};
  using mods::ImportStatus;
  const char* english = kCannotWrite;
  switch (result.status) {
    case ImportStatus::kInstalled: english = kInstalled; break;
    case ImportStatus::kCancelled: english = kCancelled; break;
    case ImportStatus::kNoModFound: english = kNoModFound; break;
    case ImportStatus::kAlreadyInstalled: english = kAlreadyInstalled; break;
    case ImportStatus::kUnreadable: english = kUnreadable; break;
    case ImportStatus::kUnsafeName: english = kUnsafeName; break;
    case ImportStatus::kUnsupportedEntry: english = kUnsupportedEntry; break;
    case ImportStatus::kNonAsciiName: english = kNonAsciiName; break;
    case ImportStatus::kNotEnoughSpace: english = kNotEnoughSpace; break;
    case ImportStatus::kCannotWrite: english = kCannotWrite; break;
  }
  return Box(tr, Text(tr, english, values));
}

MenuMessage ModPickMessage(PickProblem problem, const std::string& detail, const Translate& tr) {
  return Box(tr, Text(tr, problem == PickProblem::kNoPicker ? kNoPicker : kNotCopied,
                      {{"detail", detail}}));
}

std::vector<std::string> ModImportTexts() {
  return {kImportTitle,       kImportOk,          kImportAskSource, kImportZipButton,
          kImportFolderButton, kImportCancelButton, kImportPickZip,   kImportPickFolder,
          kImportZipFilter,   kImportBusy,        kInstalled,       kAlreadyInstalled,
          kNoModFound,        kUnreadable,        kUnsafeName,      kUnsupportedEntry,
          kNonAsciiName,      kNotEnoughSpace,    kCannotWrite,     kCancelled,
          kNoPicker,          kNotCopied};
}

}  // namespace torchlight::game_menu
