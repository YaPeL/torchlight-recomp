#include "save_import/import_plan.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>

#include "save_import/pak.h"
#include "save_import/stash.h"

namespace torchlight::save_import {
namespace fs = std::filesystem;
namespace {

constexpr const char* kStateFile = "state.txt";
constexpr std::array<const char*, 2> kStashNames = {"sharedstash.bin", "sharedstashh.bin"};

std::string Lower(std::string text) {
  for (char& c : text) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return text;
}

std::string NameOf(const fs::path& path) {
  const std::u8string name = path.filename().u8string();
  return std::string(name.begin(), name.end());
}

bool EndsWith(std::string_view text, std::string_view suffix) {
  return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

std::optional<Bytes> ReadFile(const fs::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return std::nullopt;
  return Bytes(std::istreambuf_iterator<char>(file), {});
}

// Writes next to the destination and renames over it, so a failure never leaves half a file.
bool WriteAtomic(const fs::path& path, const Bytes& data, std::string& error) {
  fs::path temporary = path;
  temporary += ".import-tmp";
  {
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    file.flush();
    if (!file) {
      error = "could not write " + NameOf(temporary);
      std::error_code ignored;
      fs::remove(temporary, ignored);
      return false;
    }
  }
  std::error_code ec;
  fs::rename(temporary, path, ec);
  if (ec) {
    error = "could not replace " + NameOf(path) + ": " + ec.message();
    fs::remove(temporary, ec);
    return false;
  }
  return true;
}

// Renames `path` to path + suffix (".2", ".3"... after it if that name is taken).
bool RenameAside(const fs::path& path, const char* suffix, std::string& error, fs::path* out = nullptr) {
  fs::path target = path;
  target += suffix;
  for (int n = 2; fs::exists(target); ++n) {
    target = path;
    target += std::string(suffix) + "." + std::to_string(n);
  }
  std::error_code ec;
  fs::rename(path, target, ec);
  if (ec) {
    error = "could not rename " + NameOf(path) + ": " + ec.message();
    return false;
  }
  if (out) *out = target;
  return true;
}

// pending/state.txt: tab-separated lines.
//   container <path>
//   stash <destination> <PC source file name> <recomp items when confirmed>
//   settings
//   notice <kind> <file> <now> <then>
struct State {
  std::optional<fs::path> container;
  struct Stash {
    std::string destination, source;
    int confirmed_items = 0;
  };
  std::vector<Stash> stashes;
  bool settings = false;
  std::vector<Notice> notices;
  bool Empty() const { return stashes.empty() && !settings && notices.empty(); }
};

std::vector<std::string> Split(const std::string& line) {
  std::vector<std::string> parts;
  std::stringstream stream(line);
  std::string part;
  while (std::getline(stream, part, '\t')) parts.push_back(part);
  return parts;
}

const char* KindName(Notice::Kind kind) {
  switch (kind) {
    case Notice::Kind::kStashChanged: return "stash_changed";
    case Notice::Kind::kContainerMissing: return "container_missing";
    case Notice::Kind::kUnreadable: return "unreadable";
  }
  return "unreadable";
}

State ReadState(const fs::path& folder) {
  State state;
  std::ifstream file(folder / kPendingFolder / kStateFile, std::ios::binary);
  std::string line;
  while (std::getline(file, line)) {
    const auto parts = Split(line);
    if (parts.empty()) continue;
    if (parts[0] == "container" && parts.size() == 2) {
      state.container = fs::path(std::u8string(parts[1].begin(), parts[1].end()));
    } else if (parts[0] == "stash" && parts.size() == 4) {
      state.stashes.push_back({parts[1], parts[2], std::atoi(parts[3].c_str())});
    } else if (parts[0] == "settings") {
      state.settings = true;
    } else if (parts[0] == "notice" && parts.size() == 5) {
      Notice notice;
      notice.kind = parts[1] == "stash_changed"       ? Notice::Kind::kStashChanged
                    : parts[1] == "container_missing" ? Notice::Kind::kContainerMissing
                                                      : Notice::Kind::kUnreadable;
      notice.file = parts[2];
      notice.now = std::atoi(parts[3].c_str());
      notice.then = std::atoi(parts[4].c_str());
      state.notices.push_back(notice);
    }
  }
  return state;
}

bool WriteState(const fs::path& folder, const State& state, std::string& error) {
  const fs::path pending = folder / kPendingFolder;
  if (state.Empty()) {
    std::error_code ec;
    fs::remove(pending / kStateFile, ec);
    if (fs::is_empty(pending, ec)) fs::remove(pending, ec);
    return true;
  }
  std::error_code ec;
  fs::create_directories(pending, ec);
  std::string text;
  if (state.container) {
    const auto path = state.container->u8string();
    text += "container\t" + std::string(path.begin(), path.end()) + "\n";
  }
  for (const auto& stash : state.stashes) {
    text += "stash\t" + stash.destination + "\t" + stash.source + "\t" +
            std::to_string(stash.confirmed_items) + "\n";
  }
  if (state.settings) text += "settings\n";
  for (const auto& notice : state.notices) {
    text += std::string("notice\t") + KindName(notice.kind) + "\t" + notice.file + "\t" +
            std::to_string(notice.now) + "\t" + std::to_string(notice.then) + "\n";
  }
  return WriteAtomic(pending / kStateFile, Bytes(text.begin(), text.end()), error);
}

// A file of the import folder by its lower-case name.
std::optional<fs::path> FindIn(const fs::path& folder, std::string_view lower_name) {
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(folder, ec)) {
    if (entry.is_regular_file(ec) && Lower(NameOf(entry.path())) == lower_name) return entry.path();
  }
  return std::nullopt;
}

int CharacterSortKey(const fs::path& path) {
  const std::string name = NameOf(path);
  return std::atoi(name.c_str());
}

std::string TextField(const Node* element, std::string_view field) {
  const Node* value = element ? element->Find(field) : nullptr;
  return value && value->kind == Node::Kind::kText ? Utf8(value->text) : "";
}

}  // namespace

bool SettingsImport::HasChanges() const {
  return std::any_of(plans.begin(), plans.end(),
                     [](const auto& entry) { return !entry.second.changes.empty(); });
}

bool ImportPlan::HasSomethingToAsk() const {
  return !blocked.empty() || !characters.empty() || !stashes.empty() || settings.has_value() ||
         !notices.empty();
}

ImportFolder ScanImportFolder(const fs::path& folder) {
  ImportFolder result;
  result.folder = folder;
  const State state = ReadState(folder);
  result.settings_pending = state.settings;
  result.notices = state.notices;
  std::set<std::string> pending_sources;
  for (const auto& stash : state.stashes) pending_sources.insert(Lower(stash.source));

  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(folder, ec)) {
    if (!entry.is_regular_file(ec)) continue;
    const std::string name = Lower(NameOf(entry.path()));
    if (EndsWith(name, ".svt")) {
      result.characters.push_back(entry.path());
    } else if (std::find(kStashNames.begin(), kStashNames.end(), name) != kStashNames.end()) {
      if (!pending_sources.contains(name)) result.stashes.push_back(entry.path());
    } else if (std::find(kSettingsFiles.begin(), kSettingsFiles.end(), name) != kSettingsFiles.end()) {
      if (!state.settings) result.settings.push_back(entry.path());
    } else if (name == "pak.zip") {
      result.pc_pak = entry.path();
    }
  }
  std::sort(result.characters.begin(), result.characters.end(), [](const fs::path& a, const fs::path& b) {
    const int ka = CharacterSortKey(a), kb = CharacterSortKey(b);
    return ka != kb ? ka < kb : NameOf(a) < NameOf(b);
  });
  std::sort(result.stashes.begin(), result.stashes.end());
  // settings.txt first, as the PC files are searched in kSettingsFiles order.
  std::sort(result.settings.begin(), result.settings.end(), [](const fs::path& a, const fs::path& b) {
    return Lower(NameOf(a)) > Lower(NameOf(b));
  });
  return result;
}

int FreeCharacterNumber(const std::vector<std::string>& file_names, const std::vector<int>& also_taken) {
  std::set<int64_t> taken(also_taken.begin(), also_taken.end());
  std::set<std::string> names;
  for (const auto& file : file_names) {
    const std::string name = Lower(file);
    names.insert(name);
    if (name.size() <= 4 || !EndsWith(name, ".tsv")) continue;
    // wcstol on the name without its extension: optional spaces and sign, then digits; 0 if none.
    const std::string stem = name.substr(0, name.size() - 4);
    taken.insert(std::strtol(stem.c_str(), nullptr, 10));
  }
  int number = 0;
  while (taken.contains(number) || names.contains(std::to_string(number) + ".tsv")) ++number;
  return number;
}

ImportPlan BuildImportPlan(const Schema& schema, const ImportFolder& folder, const fs::path& game_pak,
                           const ContainerView& container) {
  ImportPlan plan;
  plan.notices = folder.notices;
  if (!folder.HasWork()) return plan;
  if (!folder.pc_pak) {
    plan.missing_pc_pak = true;
    plan.blocked = "the PC Pak.zip is not in the import folder";
    return plan;
  }
  SaveError error;
  auto target = GameData::FromPak(game_pak, error);
  auto source = target ? GameData::FromPak(*folder.pc_pak, error) : nullptr;
  if (!target || !source) {
    plan.blocked = error.message;
    return plan;
  }

  std::vector<int> assigned;
  for (const auto& path : folder.characters) {
    CharacterImport character;
    character.source = path;
    auto data = ReadFile(path);
    std::unique_ptr<Parsed> parsed;
    if (!data) character.problems.push_back("it cannot be read");
    else if (!(parsed = ReadPc(schema, *data, error))) character.problems.push_back(error.message);
    if (parsed) {
      character.class_name = TextField(&parsed->tree, "class_name");
      character.name = TextField(parsed->tree.Find("player"), "name");
      std::map<size_t, int64_t> replacements;
      std::vector<Problem> problems;
      CheckReferences(*parsed, *target, source.get(), replacements, problems);
      for (const auto& p : problems) {
        if (IsRemovableUnit(p)) {
          character.changes.push_back("removed when imported (not in the 360 game): " + p.Text());
        } else {
          character.problems.push_back("the save references data that does not exist in the 360 game: " + p.Text());
        }
      }
      if (character.ok()) {
        ApplyReplacements(*parsed, replacements);
        AdaptQuestDialogs(*parsed, *target, *source, character.changes, character.problems);
      }
      if (character.ok()) {
        character.converted = Write360(schema, *parsed);
        std::span<const uint8_t> body;
        auto check = Split360(character.converted, body, error)
                         ? ParseBody(schema, *schema.root(), body, Endian::kBig, error)
                         : nullptr;
        if (!check) character.problems.push_back("internal error: the converted save cannot be read back");
      }
      if (character.ok()) {
        character.number = FreeCharacterNumber(container.file_names, assigned);
        assigned.push_back(character.number);
      }
    }
    plan.characters.push_back(std::move(character));
  }

  for (const auto& path : folder.stashes) {
    StashImport stash;
    stash.source = path;
    stash.destination = Lower(NameOf(path));
    auto data = ReadFile(path);
    std::unique_ptr<Parsed> parsed;
    if (!data) stash.problems.push_back("it cannot be read");
    else if (!(parsed = ReadPcStash(schema, *data, error))) stash.problems.push_back(error.message);
    if (parsed) {
      const Node* items = parsed->tree.Find("items");
      stash.pc_items = items ? static_cast<int>(items->items.size()) : 0;
      std::map<size_t, int64_t> replacements;
      std::vector<Problem> problems;
      CheckReferences(*parsed, *target, source.get(), replacements, problems);
      for (const auto& p : problems) {
        if (!IsRemovableUnit(p)) {
          stash.problems.push_back("the stash references data that does not exist in the 360 game: " + p.Text());
        }
      }
      for (const auto& p : StashSlotProblems(*parsed)) stash.problems.push_back(p);
      if (stash.ok()) {
        ApplyReplacements(*parsed, replacements);
        stash.converted = Write360Stash(schema, *parsed);
        if (!Read360Stash(schema, stash.converted, error)) {
          stash.problems.push_back("internal error: the converted stash cannot be read back");
        }
      }
      auto current = container.stashes.find(stash.destination);
      if (current != container.stashes.end()) {
        stash.recomp_items = StashItemCount(schema, current->second);
        if (stash.recomp_items < 0) stash.problems.push_back("the recomp's " + stash.destination + " cannot be read");
      }
    }
    plan.stashes.push_back(std::move(stash));
  }

  if (!folder.settings.empty()) {
    SettingsImport settings;
    settings.sources = folder.settings;
    std::vector<SettingsFile> pc;
    for (const auto& path : folder.settings) {
      SettingsFile file;
      std::string why;
      auto data = ReadFile(path);
      if (data && SettingsFile::Parse(*data, "the PC " + NameOf(path), file, why)) pc.push_back(std::move(file));
      else settings.problems.push_back(data ? why : NameOf(path) + " cannot be read");
    }
    for (const char* name : kSettingsFiles) {
      std::optional<SettingsFile> recomp;
      auto current = container.settings.find(name);
      if (current != container.settings.end()) {
        SettingsFile file;
        std::string why;
        if (SettingsFile::Parse(current->second, std::string("the recomp ") + name, file, why)) recomp = std::move(file);
        else settings.problems.push_back(why);
      }
      SettingsPlan file_plan = PlanSettingsImport(name, pc, recomp ? &*recomp : nullptr, false);
      settings.problems.insert(settings.problems.end(), file_plan.problems.begin(), file_plan.problems.end());
      settings.plans[name] = std::move(file_plan);
    }
    // Nothing to change and nothing wrong: the settings are left alone, not asked about.
    if (!settings.ok() || settings.HasChanges()) plan.settings = std::move(settings);
  }
  return plan;
}

void AppendImportLog(const fs::path& folder, const std::string& line) {
  using namespace std::chrono;
  const auto now = system_clock::now();
  const auto day = floor<days>(now);
  const year_month_day date{day};
  const hh_mm_ss time{floor<seconds>(now - day)};
  char stamp[32];
  std::snprintf(stamp, sizeof(stamp), "%04d-%02u-%02u %02ld:%02ld:%02ld UTC", int(date.year()),
                unsigned(date.month()), unsigned(date.day()), long(time.hours().count()),
                long(time.minutes().count()), long(time.seconds().count()));
  std::ofstream file(folder / kLogFile, std::ios::binary | std::ios::app);
  file << stamp << "  " << line << "\n";
}

bool ConfirmImport(const ImportPlan& plan, const ImportFolder& folder, const fs::path& container_path,
                   const std::map<std::string, bool>& replace_stash, const Log& log, std::string& error) {
  State state = ReadState(folder.folder);
  state.notices.clear();  // shown with this plan
  state.container = container_path;

  for (const auto& character : plan.characters) {
    if (character.ok()) {
      if (!RenameAside(character.source, kImportedSuffix, error)) return false;
      log(NameOf(character.source) + ": imported as " + character.destination() + " (" +
          character.name + ", " + character.class_name + ")");
      for (const auto& change : character.changes) {
        log(change.starts_with("removed") ? "  " + change : "  adapted " + change);
      }
    } else {
      if (!RenameAside(character.source, kRejectedSuffix, error)) return false;
      log(NameOf(character.source) + ": not imported");
      for (const auto& problem : character.problems) log("  " + problem);
    }
  }

  const fs::path pending = folder.folder / kPendingFolder;
  for (const auto& stash : plan.stashes) {
    if (!stash.ok()) {
      if (!RenameAside(stash.source, kRejectedSuffix, error)) return false;
      log(NameOf(stash.source) + ": not imported");
      for (const auto& problem : stash.problems) log("  " + problem);
      continue;
    }
    if (stash.needs_replace_confirmation()) {
      auto accepted = replace_stash.find(stash.destination);
      if (accepted == replace_stash.end() || !accepted->second) {
        log(NameOf(stash.source) + ": kept for later (the recomp stash has " +
            std::to_string(stash.recomp_items) + " items and replacing it was not accepted)");
        continue;
      }
    }
    std::error_code ec;
    fs::create_directories(pending, ec);
    if (!WriteAtomic(pending / stash.destination, stash.converted, error)) return false;
    state.stashes.erase(std::remove_if(state.stashes.begin(), state.stashes.end(),
                                       [&](const State::Stash& s) { return s.destination == stash.destination; }),
                        state.stashes.end());
    state.stashes.push_back({stash.destination, NameOf(stash.source), stash.recomp_items});
    log(NameOf(stash.source) + ": " + std::to_string(stash.pc_items) +
        " items, applied at the next start" +
        (stash.recomp_items ? " (replacing a recomp stash of " + std::to_string(stash.recomp_items) + " items)" : ""));
  }

  if (plan.settings) {
    if (!plan.settings->ok()) {
      for (const auto& path : plan.settings->sources) {
        if (!RenameAside(path, kRejectedSuffix, error)) return false;
      }
      log("settings: not imported");
      for (const auto& problem : plan.settings->problems) log("  " + problem);
    } else {
      state.settings = true;
      log("settings: applied at the next start");
    }
  }
  return WriteState(folder.folder, state, error);
}

bool SkipImport(const ImportPlan& plan, const ImportFolder& folder, const Log& log, std::string& error) {
  std::vector<fs::path> files;
  for (const auto& c : plan.characters) files.push_back(c.source);
  for (const auto& s : plan.stashes) files.push_back(s.source);
  if (plan.settings) files.insert(files.end(), plan.settings->sources.begin(), plan.settings->sources.end());
  if (plan.characters.empty() && plan.stashes.empty() && !plan.settings) {
    // A blocked plan (no PC pak): skip what the folder holds.
    files.insert(files.end(), folder.characters.begin(), folder.characters.end());
    files.insert(files.end(), folder.stashes.begin(), folder.stashes.end());
    files.insert(files.end(), folder.settings.begin(), folder.settings.end());
  }
  for (const auto& path : files) {
    if (!RenameAside(path, kSkippedSuffix, error)) return false;
    log(NameOf(path) + ": skipped (do not ask again)");
  }
  State state = ReadState(folder.folder);
  state.notices.clear();
  return WriteState(folder.folder, state, error);
}

std::vector<Notice> ApplyPending(const fs::path& folder, const Schema& schema, const Log& log) {
  State state = ReadState(folder);
  if (state.stashes.empty() && !state.settings) return state.notices;
  std::string error;
  const fs::path pending = folder / kPendingFolder;
  auto drop_stash = [&](const State::Stash& stash) {
    std::error_code ec;
    fs::remove(pending / stash.destination, ec);
  };

  if (!state.container || !fs::is_directory(*state.container)) {
    state.notices.push_back({Notice::Kind::kContainerMissing, "", 0, 0});
    log("pending import not applied: the save container no longer exists");
    for (const auto& stash : state.stashes) drop_stash(stash);
    state.stashes.clear();
    state.settings = false;
    WriteState(folder, state, error);
    return state.notices;
  }
  const fs::path& container = *state.container;

  for (const auto& stash : state.stashes) {
    const fs::path destination = container / stash.destination;
    int now = 0;
    if (fs::exists(destination)) {
      auto current = ReadFile(destination);
      now = current ? StashItemCount(schema, *current) : -1;
    }
    auto converted = ReadFile(pending / stash.destination);
    if (now != stash.confirmed_items || !converted) {
      state.notices.push_back({converted ? Notice::Kind::kStashChanged : Notice::Kind::kUnreadable,
                               stash.destination, now, stash.confirmed_items});
      log(stash.destination + ": not applied (the recomp stash has " + std::to_string(now) +
          " items, " + std::to_string(stash.confirmed_items) + " when it was confirmed)");
      drop_stash(stash);
      continue;
    }
    if (now > 0) {
      fs::path backup = folder / (stash.destination + ".recomp-bkp");
      for (int n = 2; fs::exists(backup); ++n) backup = folder / (stash.destination + ".recomp-bkp." + std::to_string(n));
      std::error_code ec;
      fs::copy_file(destination, backup, ec);
      if (ec) {
        state.notices.push_back({Notice::Kind::kUnreadable, stash.destination, now, stash.confirmed_items});
        log(stash.destination + ": not applied (could not back up the recomp stash: " + ec.message() + ")");
        drop_stash(stash);
        continue;
      }
      log(stash.destination + ": the recomp stash was kept as " + NameOf(backup));
    }
    if (!WriteAtomic(destination, *converted, error)) {
      state.notices.push_back({Notice::Kind::kUnreadable, stash.destination, now, stash.confirmed_items});
      log(stash.destination + ": not applied (" + error + ")");
      drop_stash(stash);
      continue;
    }
    if (auto source = FindIn(folder, Lower(stash.source))) RenameAside(*source, kImportedSuffix, error);
    log(stash.destination + ": applied");
    drop_stash(stash);
  }
  state.stashes.clear();

  if (state.settings) {
    std::vector<SettingsFile> pc;
    std::vector<fs::path> sources;
    bool readable = true;
    for (const char* name : kSettingsFiles) {
      if (auto path = FindIn(folder, name)) {
        SettingsFile file;
        std::string why;
        auto data = ReadFile(*path);
        if (!data || !SettingsFile::Parse(*data, name, file, why)) readable = false;
        else {
          pc.push_back(std::move(file));
          sources.push_back(*path);
        }
      }
    }
    for (const char* name : kSettingsFiles) {
      if (!readable) break;
      const fs::path destination = FindIn(container, name).value_or(container / name);
      std::optional<SettingsFile> recomp;
      if (fs::exists(destination)) {
        SettingsFile file;
        std::string why;
        auto data = ReadFile(destination);
        if (!data || !SettingsFile::Parse(*data, name, file, why)) {
          state.notices.push_back({Notice::Kind::kUnreadable, name, 0, 0});
          readable = false;
          break;
        }
        recomp = std::move(file);
      }
      const SettingsPlan plan = PlanSettingsImport(name, pc, recomp ? &*recomp : nullptr, false);
      if (plan.changes.empty()) continue;
      SettingsFile result = recomp ? *recomp : SettingsFile::New360();
      for (const auto& change : plan.changes) {
        result.Set(change.key, change.new_value);
        log(std::string(name) + ": " + change.key + " " + change.old_value.value_or("unset") + " -> " +
            change.new_value);
      }
      if (!WriteAtomic(destination, result.ToBytes(), error)) {
        state.notices.push_back({Notice::Kind::kUnreadable, name, 0, 0});
        readable = false;
        break;
      }
    }
    if (readable) {
      for (const auto& source : sources) RenameAside(source, kImportedSuffix, error);
      log("settings: applied");
    } else {
      if (std::none_of(state.notices.begin(), state.notices.end(),
                       [](const Notice& n) { return n.kind == Notice::Kind::kUnreadable; })) {
        state.notices.push_back({Notice::Kind::kUnreadable, "settings", 0, 0});
      }
      log("settings: not applied (a settings file could not be read or written)");
    }
    state.settings = false;
  }
  WriteState(folder, state, error);
  return state.notices;
}

}  // namespace torchlight::save_import
