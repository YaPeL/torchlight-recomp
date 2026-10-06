#include "save_import/import_message.h"

#include <utility>

namespace torchlight::save_import {
namespace {

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

std::string FileName(const std::filesystem::path& path) {
  const std::u8string name = path.filename().u8string();
  return std::string(name.begin(), name.end());
}

std::string StashName(const Translate& tr, const std::string& destination, int items) {
  const bool hardcore = destination == "sharedstashh.bin";
  return Text(tr, hardcore ? "Hardcore shared stash ({items} items)" : "Shared stash ({items} items)",
              {{"items", std::to_string(items)}});
}

void Section(std::string& text, const std::string& heading, const std::vector<std::string>& lines,
             const std::string& summary) {
  if (lines.empty()) return;
  text += "\n" + heading + "\n";
  if (lines.size() > kMaxListedFiles) {
    text += "  " + summary + "\n";
    return;
  }
  for (const auto& line : lines) text += "  " + line + "\n";
}

std::string NoticeText(const Notice& notice, const Translate& tr) {
  switch (notice.kind) {
    case Notice::Kind::kStashChanged:
      return Text(tr,
                  "The stash from the PC version was not applied when the game started: the "
                  "shared stash now has {now} items ({then} when you confirmed).",
                  {{"now", std::to_string(notice.now)}, {"then", std::to_string(notice.then)}});
    case Notice::Kind::kContainerMissing:
      return Text(tr, "What you confirmed last time was not applied: the save folder it was "
                      "meant for no longer exists.");
    case Notice::Kind::kUnreadable:
      return Text(tr, "What you confirmed last time was not applied: {file} could not be read or "
                      "written (see import.log).",
                  {{"file", notice.file}});
  }
  return "";
}

}  // namespace

ImportMessage MainImportMessage(const ImportPlan& plan, const std::filesystem::path& import_folder,
                                const Translate& tr) {
  ImportMessage message;
  message.title = Text(tr, "Import from the PC version");
  std::string& text = message.text;
  for (const auto& notice : plan.notices) text += NoticeText(notice, tr) + "\n\n";

  const std::u8string folder = import_folder.u8string();
  if (plan.missing_pc_pak) {
    text += Text(tr, "There are files from the PC version in the import folder. To import them, "
                     "copy into that folder the Pak.zip of the PC version (it is next to "
                     "Torchlight.exe):") +
            "\n" + std::string(folder.begin(), folder.end());
  } else if (!plan.blocked.empty()) {
    text += Text(tr, "The files in the import folder cannot be imported: {reason}",
                 {{"reason", plan.blocked}});
  }
  if (!plan.blocked.empty()) {
    message.buttons = {Text(tr, "Not now"), Text(tr, "Do not ask again")};
    message.active = 0;
    return message;
  }

  std::vector<std::string> now, later, rejected;
  int imported = 0;
  for (const auto& character : plan.characters) {
    if (character.ok()) {
      ++imported;
      now.push_back(Text(tr, "{name} ({class}), slot {slot}",
                         {{"name", character.name}, {"class", character.class_name},
                          {"slot", std::to_string(character.number)}}));
    } else {
      rejected.push_back(FileName(character.source));
    }
  }
  for (const auto& stash : plan.stashes) {
    if (stash.ok()) later.push_back(StashName(tr, stash.destination, stash.pc_items));
    else rejected.push_back(FileName(stash.source));
  }
  if (plan.settings) {
    if (plan.settings->ok()) later.push_back(Text(tr, "Settings (volumes, automap, tips...)"));
    else for (const auto& path : plan.settings->sources) rejected.push_back(FileName(path));
  }

  text += Text(tr, "There are files from the PC version in the import folder.") + "\n";
  Section(text, Text(tr, "Imported now:"), now,
          Text(tr, "{count} characters", {{"count", std::to_string(imported)}}));
  Section(text, Text(tr, "Applied the next time you open the game:"), later,
          Text(tr, "{count} files", {{"count", std::to_string(later.size())}}));
  Section(text, Text(tr, "Cannot be imported (see import.log in the import folder):"), rejected,
          Text(tr, "{count} files", {{"count", std::to_string(rejected.size())}}));
  text += "\n" + Text(tr, "Import them?");
  message.buttons = {Text(tr, "Yes"), Text(tr, "Not now"), Text(tr, "Do not ask again")};
  message.active = 1;
  return message;
}

MainAnswer MainImportAnswer(const ImportPlan& plan, int button) {
  if (!plan.blocked.empty()) return button == 1 ? MainAnswer::kDoNotAskAgain : MainAnswer::kNotNow;
  if (button == 0) return MainAnswer::kYes;
  if (button == 2) return MainAnswer::kDoNotAskAgain;
  return MainAnswer::kNotNow;
}

ImportMessage ReplaceStashMessage(const StashImport& stash, const Translate& tr) {
  ImportMessage message;
  message.title = Text(tr, "Import from the PC version");
  message.text = Text(tr,
                      "The {stash} already has {recomp} items. Replace it with the one from the PC "
                      "version ({pc} items)? The current one is kept in the import folder.",
                      {{"stash", stash.destination == "sharedstashh.bin" ? Text(tr, "hardcore shared stash")
                                                                         : Text(tr, "shared stash")},
                       {"recomp", std::to_string(stash.recomp_items)},
                       {"pc", std::to_string(stash.pc_items)}});
  message.buttons = {Text(tr, "No"), Text(tr, "Replace")};
  message.active = 0;
  return message;
}

}  // namespace torchlight::save_import
