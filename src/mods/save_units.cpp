#include "mods/save_units.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>
#include <system_error>
#include <utility>

#include "mods/save_safety.h"
#include "save_import/pak.h"

namespace torchlight::mods {

namespace si = torchlight::save_import;

namespace {

bool IsNone(int64_t guid) { return guid == -1 || guid == 0; }

// Where each node sits: the list holding it, for the elements and values of lists.
using Parents = std::map<const si::Node*, std::pair<si::Node*, std::list<si::Node>::iterator>>;

void MapParents(si::Node& node, Parents& parents) {
  for (auto& [name, child] : node.fields) MapParents(child, parents);
  for (auto it = node.items.begin(); it != node.items.end(); ++it) {
    parents[&*it] = {&node, it};
    MapParents(*it, parents);
  }
}

std::string NameOf(const si::Node* element) {
  if (!element) return {};
  for (const char* key : {"s0", "name"}) {
    if (const si::Node* n = element->Find(key); n && n->kind == si::Node::Kind::kText && !n->text.empty()) {
      return si::Utf8(n->text);
    }
  }
  return {};
}

std::string ParentPath(const std::string& path) {
  const size_t slash = path.rfind('/');
  return slash == std::string::npos ? path : path.substr(0, slash);
}

}  // namespace

KnownUnits MakeKnownUnits(const std::optional<UnitIndex>& base, const std::vector<int64_t>& mod_guids,
                          bool mods_complete, const std::string& mods_why) {
  KnownUnits known;
  if (!base) {
    known.incomplete_why = "the game's unit index was not read";
    return known;
  }
  for (const auto& group : base->groups) {
    if (group.empty()) {
      known.incomplete_why = "the game's unit index lacks a group";
      return known;
    }
    for (const UnitEntry& e : group) known.guids.insert(e.guid);
  }
  for (int64_t guid : mod_guids) known.guids.insert(guid);
  if (!mods_complete) {
    known.incomplete_why = mods_why.empty() ? "the mods' units were not all read" : mods_why;
    return known;
  }
  known.complete = true;
  return known;
}

KnownUnits KnownUnitsOfIndex(const UnitIndex& loaded) { return MakeKnownUnits(loaded, {}, true); }

KnownUnits MakeExpectedUnits(const std::optional<UnitIndex>& base,
                             const std::optional<std::vector<UnitEntry>>& mod_units, const std::string& mods_why) {
  if (!base || !mod_units) return MakeKnownUnits(base, {}, mod_units.has_value(), mods_why);
  KnownUnits known = MakeKnownUnits(base, {}, true);
  if (!known.complete) return known;
  std::vector<std::u16string> skipped;
  return KnownUnitsOfIndex(MergeUnitIndex(*base, *mod_units, &skipped));
}

std::vector<int64_t> UnitsNotLoaded(const KnownUnits& assumed, const std::unordered_set<int64_t>& loaded) {
  std::vector<int64_t> out;
  for (int64_t guid : assumed.guids) {
    if (!loaded.contains(guid)) out.push_back(guid);
  }
  std::sort(out.begin(), out.end());
  return out;
}

bool PlayerOwnedUnitRef(std::string_view path, bool stash_file) {
  if (stash_file) return true;
  for (std::string_view other : {"levels/level/units/", "quests/", "guids2/"}) {
    if (path.starts_with(other)) return false;
  }
  return true;
}

UnitCheck RemoveUnknownUnits(si::Parsed& parsed, const KnownUnits& known, bool stash_file) {
  UnitCheck check;
  Parents parents;
  MapParents(parsed.tree, parents);
  size_t units = 0;
  std::vector<std::pair<const si::Ref*, const si::Node*>> unknown;  // ref, node to remove
  for (const si::Ref& ref : parsed.refs) {
    if (ref.role != "unit_guid") continue;
    const int64_t guid = si::Signed64(ref.value->number);
    if (IsNone(guid)) continue;
    if (!ref.only_if_set.empty()) {
      const si::Node* guard = ref.element ? ref.element->Find(ref.only_if_set) : nullptr;
      if (IsNone(guard ? si::Signed64(guard->number) : -1)) continue;  // e.g. gold piles
    }
    ++units;
    if (known.guids.contains(guid)) continue;
    if (!PlayerOwnedUnitRef(ref.path, stash_file)) {
      check.kept_level.push_back({ref.path, NameOf(ref.element), guid});
      continue;
    }
    // The entry to remove: an item or a unit in a list, or the GUID itself in a list of GUIDs.
    // Anything else (the character's own class, a quest's unit) cannot go.
    const bool in_guid_list = parents.contains(ref.value);
    const bool item_or_unit = ref.path.ends_with("/item/unit_guid") || ref.path.ends_with("/unit/unit_guid");
    const si::Node* target = in_guid_list ? ref.value : (item_or_unit ? ref.element : nullptr);
    if (!target || !parents.contains(target)) {
      check.result = UnitCheck::Result::kLeftAlone;
      check.why = "unknown unit " + std::to_string(guid) + " at " + ref.path + ", which cannot be removed";
      return check;
    }
    unknown.emplace_back(&ref, target);
  }
  // Every unknown unit counts here, the player's or not: most of a save unknown is a fault of ours.
  const size_t all_unknown = unknown.size() + check.kept_level.size();
  if (all_unknown && 2 * all_unknown > units) {
    check.result = UnitCheck::Result::kLeftAlone;
    check.why = std::to_string(all_unknown) + " of " + std::to_string(units) +
                " units unknown: too many to be a removed mod's";
    return check;
  }
  if (unknown.empty()) return check;
  if (!known.complete) {
    check.result = UnitCheck::Result::kLeftAlone;
    check.why = std::to_string(unknown.size()) + " unknown units, but " + known.incomplete_why;
    return check;
  }
  for (const auto& [ref, target] : unknown) {
    const si::Node* element = ref->element ? ref->element : target;
    check.removed.push_back({ref->value == target ? ref->path : ParentPath(ref->path), NameOf(element),
                             si::Signed64(ref->value->number)});
  }
  // Remove children before their parents (an item inside a removed creature goes with it): erase
  // what is still reachable, in reverse discovery order.
  for (auto it = unknown.rbegin(); it != unknown.rend(); ++it) {
    auto found = parents.find(it->second);
    if (found == parents.end()) continue;
    auto [list, position] = found->second;
    // Forget everything inside the erased node, so a later target inside it is skipped.
    Parents inside;
    MapParents(*position, inside);
    for (const auto& [node, unused] : inside) parents.erase(node);
    parents.erase(found);
    list->items.erase(position);
  }
  check.result = UnitCheck::Result::kRemoved;
  return check;
}

UnitCheck CheckCharacterFile(const si::Schema& schema, std::span<const uint8_t> file, const KnownUnits& known) {
  UnitCheck check;
  si::SaveError error;
  std::span<const uint8_t> body;
  std::unique_ptr<si::Parsed> parsed;
  if (si::Split360(file, body, error)) {
    parsed = si::ParseBody(schema, *schema.root(), body, si::Endian::kBig, error);
  }
  if (!parsed) {
    check.result = UnitCheck::Result::kLeftAlone;
    check.why = "unreadable: " + error.message;
    return check;
  }
  check = RemoveUnknownUnits(*parsed, known, false);
  check.owner = NameOf(parsed->tree.Find("player"));
  if (check.result == UnitCheck::Result::kRemoved) check.bytes = si::Write360(schema, *parsed);
  return check;
}

UnitCheck CheckStashFile(const si::Schema& schema, std::span<const uint8_t> file, const KnownUnits& known) {
  UnitCheck check;
  si::SaveError error;
  auto parsed = si::Read360Stash(schema, file, error);
  if (!parsed) {
    check.result = UnitCheck::Result::kLeftAlone;
    check.why = "unreadable: " + error.message;
    return check;
  }
  check = RemoveUnknownUnits(*parsed, known, true);
  if (check.result == UnitCheck::Result::kRemoved) check.bytes = si::Write360Stash(schema, *parsed);
  return check;
}

namespace {

std::string Lower(std::string s) {
  for (char& c : s) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
  return s;
}

std::optional<std::vector<uint8_t>> ReadAll(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
}

bool WriteWhole(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
  std::error_code ec;
  const std::filesystem::path temp = path.string() + ".units.tmp";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    out.flush();
    if (!out) {
      std::filesystem::remove(temp, ec);
      return false;
    }
  }
  std::filesystem::rename(temp, path, ec);
  if (ec) std::filesystem::remove(temp, ec);
  return !ec;
}

// Each character file (*.tsv, any case) and shared stash (sharedstash*.bin) in every
// <user_data_root>/<profile>/<title_folder>/, the backups left out.
void ForEachSaveFile(const std::filesystem::path& user_data_root, const std::string& title_folder,
                     const std::function<void(const std::filesystem::path&, bool character)>& visit) {
  namespace fs = std::filesystem;
  std::error_code ec;
  for (const auto& profile : fs::directory_iterator(user_data_root, ec)) {
    std::error_code type_ec;
    if (!profile.is_directory(type_ec) || profile.path().filename() == "save-backups") continue;
    const fs::path container = profile.path() / title_folder;
    if (!fs::is_directory(container, type_ec)) continue;
    for (auto it = fs::recursive_directory_iterator(container, type_ec);
         !type_ec && it != fs::recursive_directory_iterator(); it.increment(type_ec)) {
      std::error_code file_ec;
      if (!it->is_regular_file(file_ec)) continue;
      const std::string name = Lower(it->path().filename().string());
      const bool character = name.size() > 4 && name.ends_with(".tsv");
      const bool stash = name.starts_with("sharedstash") && name.ends_with(".bin");
      if (character || stash) visit(it->path(), character);
    }
  }
}

}  // namespace

SaveUnitsReport ProtectSaves(const std::filesystem::path& user_data_root, const std::string& title_folder,
                             const si::Schema& schema, const KnownUnits& known,
                             std::chrono::system_clock::time_point now,
                             const std::function<void(const std::string&)>& log) {
  namespace fs = std::filesystem;
  SaveUnitsReport report;
  std::vector<std::pair<fs::path, UnitCheck>> changes;
  ForEachSaveFile(user_data_root, title_folder, [&](const fs::path& path, bool character) {
    const auto bytes = ReadAll(path);
    if (!bytes) {
      report.left_alone.push_back(path.string() + ": cannot read the file");
      return;
    }
    UnitCheck check = character ? CheckCharacterFile(schema, *bytes, known) : CheckStashFile(schema, *bytes, known);
    for (const RemovedUnit& r : check.kept_level) {
      if (log) {
        log("units: " + path.string() + ": unknown " + r.path + " " + std::to_string(r.guid) +
            (r.name.empty() ? "" : " (" + r.name + ")") + " left in the save: not the player's");
      }
    }
    if (check.result == UnitCheck::Result::kLeftAlone) {
      report.left_alone.push_back(path.string() + ": " + check.why);
    } else if (check.result == UnitCheck::Result::kRemoved) {
      changes.emplace_back(path, std::move(check));
    }
  });
  for (const auto& line : report.left_alone) {
    if (log) log("units: left alone " + line);
  }
  if (changes.empty()) return report;

  const SaveBackup backup = BackUpSaves(user_data_root, title_folder, now, log, "units");
  if (backup.result != SaveBackup::Result::kDone) {
    report.failed = true;
    if (log) log("units: no backup, so no save is changed");
    return report;
  }
  report.backup = backup.folder;
  for (auto& [path, check] : changes) {
    for (const RemovedUnit& r : check.removed) {
      if (log) {
        log("units: " + path.string() + ": removed " + r.path + " " + std::to_string(r.guid) +
            (r.name.empty() ? "" : " (" + r.name + ")"));
      }
    }
    if (!WriteWhole(path, check.bytes)) {
      report.failed = true;
      if (log) log("units: cannot write " + path.string() + "; the saves before are in " + backup.folder.string());
      return report;
    }
    report.changed.push_back({path, check.owner, std::move(check.removed)});
  }
  return report;
}

std::vector<SaveHolding> SavesHoldingUnits(const std::filesystem::path& user_data_root, const std::string& title_folder,
                                           const si::Schema& schema, const std::unordered_set<int64_t>& guids) {
  std::vector<SaveHolding> out;
  ForEachSaveFile(user_data_root, title_folder, [&](const std::filesystem::path& path, bool character) {
    const auto bytes = ReadAll(path);
    if (!bytes) return;
    si::SaveError error;
    std::unique_ptr<si::Parsed> parsed;
    if (character) {
      std::span<const uint8_t> body;
      if (si::Split360(*bytes, body, error)) parsed = si::ParseBody(schema, *schema.root(), body, si::Endian::kBig, error);
    } else {
      parsed = si::Read360Stash(schema, *bytes, error);
    }
    if (!parsed) return;
    for (const si::Ref& ref : parsed->refs) {
      if (ref.role == "unit_guid" && guids.contains(si::Signed64(ref.value->number)) &&
          PlayerOwnedUnitRef(ref.path, !character)) {
        out.push_back({path, character ? NameOf(parsed->tree.Find("player")) : std::string()});
        return;
      }
    }
  });
  std::sort(out.begin(), out.end(), [](const SaveHolding& a, const SaveHolding& b) { return a.path < b.path; });
  return out;
}

void ProtectFromUnitsNotLoaded(const std::filesystem::path& user_data_root, const std::string& title_folder,
                               const si::Schema& schema, const std::vector<int64_t>& missing,
                               std::chrono::system_clock::time_point now,
                               const std::function<void(const std::string&)>& log, SaveUnitsReport& report) {
  const std::unordered_set<int64_t> guids(missing.begin(), missing.end());
  const auto holding = SavesHoldingUnits(user_data_root, title_folder, schema, guids);
  if (holding.empty()) return;
  report.saving_blocked = true;
  for (const SaveHolding& h : holding) {
    report.holding_not_loaded.push_back(h.owner.empty() ? std::string()
                                                        : h.owner + " (" + h.path.filename().string() + ")");
    if (log) log("units: " + h.path.string() + " holds units the game did not load");
  }
  const SaveBackup backup = BackUpSaves(user_data_root, title_folder, now, log, "units-not-loaded");
  if (backup.result == SaveBackup::Result::kDone) report.not_loaded_backup = backup.folder;
}

}  // namespace torchlight::mods
