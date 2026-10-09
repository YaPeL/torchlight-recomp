#include "mods/save_units_notice.h"

#include <string>
#include <utility>
#include <vector>

namespace torchlight::mods {

namespace {

std::string Text(const save_import::Translate& tr, const std::string& english,
                 const std::vector<std::pair<std::string, std::string>>& values = {}) {
  std::string text = tr(english);
  for (const auto& [key, value] : values) {
    const std::string placeholder = "{" + key + "}";
    for (size_t at = text.find(placeholder); at != std::string::npos; at = text.find(placeholder, at + value.size())) {
      text.replace(at, placeholder.size(), value);
    }
  }
  return text;
}

std::string Utf8Path(const std::filesystem::path& path) {
  const std::u8string s = path.u8string();
  return std::string(s.begin(), s.end());
}

}  // namespace

std::optional<save_import::ImportMessage> SaveUnitsNotice(const SaveUnitsReport& report,
                                                          const save_import::Translate& tr) {
  // Left alone with unknown units (not merely unreadable files): those may still stop the game.
  std::vector<std::string> risky;
  for (const std::string& line : report.left_alone) {
    if (line.find("unknown unit") != std::string::npos || line.find(" units unknown") != std::string::npos) {
      const size_t colon = line.find(": ");
      risky.push_back(Utf8Path(std::filesystem::path(line.substr(0, colon)).filename()));
    }
  }
  if (report.changed.empty() && risky.empty() && !report.failed && !report.units_not_loaded &&
      !report.saving_blocked) {
    return std::nullopt;
  }

  save_import::ImportMessage message;
  if (report.saving_blocked) {
    message.title = Text(tr, "Nothing will be saved in this session");
  } else if (report.changed.empty() && report.units_not_loaded) {
    message.title = Text(tr, "Items from mods not loaded");
  } else {
    message.title = Text(tr, "Items removed from saved characters");
  }
  std::string text;
  const std::string count = std::to_string(report.units_not_loaded);
  if (report.saving_blocked) {
    text += Text(tr, "{count} kinds of items or creatures from mods could not be loaded this time, and these saves "
                     "carry them:",
                 {{"count", count}}) + "\n";
    for (const std::string& owner : report.holding_not_loaded) {
      text += "  " + (owner.empty() ? Text(tr, "Shared stash") : owner) + "\n";
    }
    text += Text(tr, "So that they do not lose those items, NOTHING WILL BE SAVED IN THIS SESSION: no progress, no "
                     "new characters, no changes to the stash. Quit the game and see the log.") + "\n";
    if (!report.not_loaded_backup.empty()) {
      text += Text(tr, "A copy of the saves is in:") + "\n  " + Utf8Path(report.not_loaded_backup) + "\n";
    } else {
      text += Text(tr, "The saves could not be copied (see the log).") + "\n";
    }
  } else if (report.units_not_loaded) {
    text += Text(tr, "{count} kinds of items or creatures from mods could not be loaded this time, so the game does "
                     "not know them; no save carries them (see the log).",
                 {{"count", count}}) + "\n";
  }
  if (!report.changed.empty()) {
    size_t total = 0;
    for (const auto& file : report.changed) total += file.removed.size();
    text += Text(tr, "{count} items from mods that are no longer installed were removed so these saved characters can load:",
                 {{"count", std::to_string(total)}}) + "\n";
    for (const auto& file : report.changed) {
      const std::string count = std::to_string(file.removed.size());
      text += "  " + (file.owner.empty() ? Text(tr, "Shared stash: {count} items", {{"count", count}})
                                          : Text(tr, "{name}: {count} items", {{"name", file.owner}, {"count", count}})) +
              "\n";
    }
    text += Text(tr, "A copy of the saves from before is in:") + "\n  " + Utf8Path(report.backup) + "\n";
  }
  if (report.failed) {
    text += Text(tr, "The saves could not be backed up or written, so not every save was changed; "
                     "characters holding items of removed mods may not load (see the log).") + "\n";
  }
  if (!risky.empty()) {
    text += Text(tr, "These saves hold items the game does not know but were left unchanged, so they "
                     "may not load (see the log):") + "\n";
    for (const auto& name : risky) text += "  " + name + "\n";
  }
  message.text = text;
  message.buttons = {Text(tr, "OK")};
  message.active = 0;
  return message;
}

}  // namespace torchlight::mods
