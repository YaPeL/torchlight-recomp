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

// A backup folder as the player finds it: from the user data folder on (save-backups/<name>,
// save_safety.h), or the whole path for one elsewhere.
std::string BackupPath(const std::filesystem::path& folder) {
  // Always with "/" (a path's own separator is "\" on Windows): the text is the same everywhere.
  if (folder.parent_path().filename() == "save-backups") return "save-backups/" + Utf8Path(folder.filename());
  return Utf8Path(folder);
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
    message.title = Text(tr, "Saving is off this session");
  } else if (report.changed.empty() && report.units_not_loaded) {
    message.title = Text(tr, "Mod items not loaded");
  } else {
    message.title = Text(tr, "Mod items removed");
  }
  std::string text;
  const std::string count = std::to_string(report.units_not_loaded);
  if (report.saving_blocked) {
    text += Text(tr, "{count} kinds of mod items did not load. These saves have them:", {{"count", count}}) + "\n";
    for (const std::string& owner : report.holding_not_loaded) {
      text += "  " + (owner.empty() ? Text(tr, "Shared stash") : owner) + "\n";
    }
    text += Text(tr, "NOTHING WILL BE SAVED THIS SESSION, so they keep those items.") + "\n";
    text += Text(tr, "Quit the game and see the log.") + "\n";
    if (!report.not_loaded_backup.empty()) {
      text += Text(tr, "Backup: {path}", {{"path", BackupPath(report.not_loaded_backup)}}) + "\n";
    } else {
      text += Text(tr, "The saves could not be copied (see the log).") + "\n";
    }
  } else if (report.units_not_loaded) {
    text += Text(tr, "{count} kinds of mod items did not load this time (see the log). No save has them.",
                 {{"count", count}}) + "\n";
  }
  if (!report.changed.empty()) {
    size_t total = 0;
    for (const auto& file : report.changed) total += file.removed.size();
    text += Text(tr, "Removed {count} items from mods that are no longer installed:",
                 {{"count", std::to_string(total)}}) + "\n";
    for (const auto& file : report.changed) {
      const std::string count = std::to_string(file.removed.size());
      text += "  " + (file.owner.empty() ? Text(tr, "Shared stash: {count}", {{"count", count}})
                                          : Text(tr, "{name}: {count}", {{"name", file.owner}, {"count", count}})) +
              "\n";
    }
    text += Text(tr, "Backup: {path}", {{"path", BackupPath(report.backup)}}) + "\n";
  }
  if (report.failed) {
    text += Text(tr, "Some saves could not be backed up or changed (see the log).") + "\n";
  }
  if (!risky.empty()) {
    text += Text(tr, "These saves have unknown items and were left unchanged (see the log):") + "\n";
    for (const auto& name : risky) text += "  " + name + "\n";
  }
  message.text = text;
  message.buttons = {Text(tr, "OK")};
  message.active = 0;
  return message;
}

}  // namespace torchlight::mods
