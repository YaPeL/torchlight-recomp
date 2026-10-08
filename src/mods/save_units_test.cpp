// Taking unknown units out of saves (save_units.h) on the synthetic PC save of the import tests
// (tests/save_import/fixtures, made by the Python tool; no game data): an unknown item removed and
// the file still read back, and every safeguard leaving the save alone: an incomplete or broken
// index, most units unknown, the character's own class unknown, an unreadable file.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>

#include "mods/save_units.h"
#include "mods/save_units_notice.h"
#include "save_import/schema.h"

using namespace torchlight::mods;
namespace si = torchlight::save_import;

namespace {
int failures = 0;
void Check(bool ok, const std::string& what) {
  if (!ok) {
    ++failures;
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
  }
}

si::Bytes Fixture(const char* name) {
  std::ifstream file(std::filesystem::path(SAVE_IMPORT_FIXTURES) / name, std::ios::binary);
  return si::Bytes(std::istreambuf_iterator<char>(file), {});
}

// Every non-none unit GUID the save references.
std::set<int64_t> UnitGuids(const si::Parsed& parsed) {
  std::set<int64_t> out;
  for (const si::Ref& ref : parsed.refs) {
    if (ref.role != "unit_guid") continue;
    const int64_t g = si::Signed64(ref.value->number);
    if (g != 0 && g != -1) out.insert(g);
  }
  return out;
}

UnitIndex IndexWith(const std::set<int64_t>& guids) {
  UnitIndex index;
  for (auto& group : index.groups) group.push_back({});  // every group present
  for (int64_t g : guids) {
    UnitEntry e;
    e.guid = g;
    index.groups[0].push_back(e);
  }
  return index;
}

// A fresh parse of the fixture as a 360 character file, and one of its items given `guid`.
std::unique_ptr<si::Parsed> Parse(const si::Schema& schema, si::Bytes& x360) {
  si::SaveError error;
  std::span<const uint8_t> body;
  if (!si::Split360(x360, body, error)) return nullptr;
  return si::ParseBody(schema, *schema.root(), body, si::Endian::kBig, error);
}
si::Node* PlayerItem(si::Parsed& parsed) {
  si::Node* items = parsed.tree.Find("player")->Find("items");
  return items && !items->items.empty() ? &items->items.front() : nullptr;
}
}  // namespace

int main() {
  std::string error;
  const auto schema = si::Schema::Embedded(error);
  Check(schema != nullptr, "schema: " + error);
  si::SaveError save_error;
  auto pc = si::ReadPc(*schema, Fixture("pc_save.svt"), save_error);
  Check(pc != nullptr, "fixture: " + save_error.message);
  if (!schema || !pc) return EXIT_FAILURE;

  const int64_t kMod = 0x0777000000000001;  // a removed mod's item
  si::Node* item = PlayerItem(*pc);
  Check(item && item->Find("unit_guid"), "the fixture's player has an item");
  item->Find("unit_guid")->number = static_cast<uint64_t>(kMod);
  si::Bytes x360 = si::Write360(*schema, *pc);
  auto parsed = Parse(*schema, x360);
  const std::set<int64_t> all = UnitGuids(*parsed);
  std::set<int64_t> base = all;
  base.erase(kMod);
  const size_t items_before = PlayerItem(*parsed) ? parsed->tree.Find("player")->Find("items")->items.size() : 0;

  // Everything known: clean.
  {
    auto p = Parse(*schema, x360);
    const auto check = RemoveUnknownUnits(*p, MakeKnownUnits(IndexWith(all), {}, true));
    Check(check.result == UnitCheck::Result::kClean && check.removed.empty(), "all known: clean");
  }
  // The mod item unknown: removed, the rest kept, the file reads back.
  {
    const auto check = CheckCharacterFile(*schema, x360, MakeKnownUnits(IndexWith(base), {}, true));
    Check(check.result == UnitCheck::Result::kRemoved, "unknown item: removed");
    Check(check.removed.size() == 1 && check.removed[0].guid == kMod &&
              check.removed[0].path == "player/items/item",
          "the removed item listed with its place");
    si::Bytes out = check.bytes;
    auto back = Parse(*schema, out);
    Check(back != nullptr, "the new file reads back with its digest");
    if (back) {
      Check(back->tree.Find("player")->Find("items")->items.size() + 1 == items_before, "one item fewer");
      Check(!UnitGuids(*back).contains(kMod), "the unknown GUID is gone");
    }
    // The same through the mods' GUIDs instead of the base.
    const auto via_mods = CheckCharacterFile(*schema, x360, MakeKnownUnits(IndexWith(base), {kMod}, true));
    Check(via_mods.result == UnitCheck::Result::kClean, "a mod's GUID counts as known");
  }
  // A mod defines kMod but its unit did not get into the index the game loads (e.g. the game
  // could not load the definition): to the game, and so to the check, it is unknown.
  {
    UnitIndex loaded = IndexWith(base);  // the merged index as built: kMod missing
    const auto check = CheckCharacterFile(*schema, x360, KnownUnitsOfIndex(loaded));
    Check(check.result == UnitCheck::Result::kRemoved && check.removed.size() == 1 && check.removed[0].guid == kMod,
          "a mod's unit missing from the loaded index is removed, whatever its definition says");
    loaded.groups[0].push_back({});
    loaded.groups[0].back().guid = kMod;
    Check(CheckCharacterFile(*schema, x360, KnownUnitsOfIndex(loaded)).result == UnitCheck::Result::kClean,
          "once in the loaded index it is kept");
  }
  // Safeguards: nothing is removed.
  {
    const auto incomplete = CheckCharacterFile(*schema, x360, MakeKnownUnits(IndexWith(base), {}, false, "a mod unreadable"));
    Check(incomplete.result == UnitCheck::Result::kLeftAlone && incomplete.bytes.empty() &&
              incomplete.why.find("a mod unreadable") != std::string::npos,
          "mods' units incomplete: left alone");
    const auto no_index = CheckCharacterFile(*schema, x360, MakeKnownUnits(std::nullopt, {}, true));
    Check(no_index.result == UnitCheck::Result::kLeftAlone && no_index.bytes.empty(), "no base index: left alone");
    UnitIndex broken = IndexWith(base);
    broken.groups[2].clear();
    const auto missing_group = CheckCharacterFile(*schema, x360, MakeKnownUnits(broken, {}, true));
    Check(missing_group.result == UnitCheck::Result::kLeftAlone, "an index without a group: left alone");
    std::set<int64_t> few;
    few.insert(*base.begin());
    const auto most = CheckCharacterFile(*schema, x360, MakeKnownUnits(IndexWith(few), {}, true));
    Check(most.result == UnitCheck::Result::kLeftAlone && most.bytes.empty(), "most units unknown: left alone");
    const int64_t player_class = si::Signed64(parsed->tree.Find("player")->Find("unit_guid")->number);
    std::set<int64_t> no_class = base;
    no_class.erase(player_class);
    const auto class_unknown = CheckCharacterFile(*schema, x360, MakeKnownUnits(IndexWith(no_class), {}, true));
    Check(class_unknown.result == UnitCheck::Result::kLeftAlone && class_unknown.bytes.empty(),
          "the character's own class unknown: left alone");
    si::Bytes garbage(100, 0x5A);
    const auto unreadable = CheckCharacterFile(*schema, garbage, MakeKnownUnits(IndexWith(base), {}, true));
    Check(unreadable.result == UnitCheck::Result::kLeftAlone && unreadable.why.starts_with("unreadable"),
          "unreadable file: left alone");
  }

  // The whole pass over a user data folder: backup first, the file rewritten, the rest untouched;
  // with incomplete known units nothing is written and no backup is made.
  {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() /
        ("tl_save_units_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path saves = root / "B13EBABEBABEBABE" / "58410A7E" / "00000001" / "torchlight.sav";
    fs::create_directories(saves);
    auto write = [](const fs::path& p, const si::Bytes& b) {
      std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
    };
    write(saves / "0.TSV", x360);
    write(saves / "settings.txt", si::Bytes{'x'});
    std::vector<std::string> lines;
    auto log = [&](const std::string& l) { lines.push_back(l); };
    const auto now = std::chrono::system_clock::now();

    const auto none = ProtectSaves(root, "58410A7E", *schema, MakeKnownUnits(IndexWith(base), {}, false), now, log);
    Check(none.changed.empty() && none.backup.empty() && none.left_alone.size() == 1 && !fs::exists(root / "save-backups"),
          "incomplete known units: nothing written, no backup, the file listed as left alone");

    const auto done = ProtectSaves(root, "58410A7E", *schema, MakeKnownUnits(IndexWith(base), {}, true), now, log);
    Check(!done.failed && done.changed.size() == 1 && done.changed[0].removed.size() == 1, "one file changed");
    Check(!done.backup.empty() && done.backup.filename().string().ends_with("-units") &&
              fs::exists(done.backup / "B13EBABEBABEBABE" / "58410A7E" / "00000001" / "torchlight.sav" / "0.TSV"),
          "the saves backed up first, the file as it was");
    std::ifstream back(saves / "0.TSV", std::ios::binary);
    si::Bytes now_bytes((std::istreambuf_iterator<char>(back)), {});
    auto reparsed = Parse(*schema, now_bytes);
    Check(reparsed && !UnitGuids(*reparsed).contains(kMod), "the file on disk no longer has the item");
    Check(!fs::exists(saves / "0.TSV.units.tmp"), "no temporary file left");
    const auto again = ProtectSaves(root, "58410A7E", *schema, MakeKnownUnits(IndexWith(base), {}, true), now, log);
    Check(again.changed.empty() && again.backup.empty(), "a second pass finds nothing and backs nothing up");
    fs::remove_all(root);
  }

  // The player's notice: nothing to tell, the removed items with the backup, risky saves.
  {
    const auto english = [](const std::string& s) { return s; };
    Check(!SaveUnitsNotice(SaveUnitsReport{}, english), "nothing changed: no notice");
    SaveUnitsReport report;
    report.backup = "/saves/save-backups/20261008-120000-units";
    report.changed.push_back({"/saves/a/4.TSV", "Ronan", {{"player/items/item", "", 1}, {"pets/unit", "", 2}}});
    report.changed.push_back({"/saves/a/sharedstash.bin", "", {{"items/item", "", 3}}});
    report.left_alone.push_back("/saves/a/7.TSV: unknown unit 5 at player/unit_guid, which cannot be removed");
    report.left_alone.push_back("/saves/a/9.TSV: unreadable: bad digest");
    const auto notice = SaveUnitsNotice(report, english);
    Check(notice && notice->buttons.size() == 1, "a notice with one button");
    if (notice) {
      Check(notice->text.find("3 items from mods") != std::string::npos, "the total");
      Check(notice->text.find("Ronan: 2 items") != std::string::npos, "per character");
      Check(notice->text.find("Shared stash: 1 items") != std::string::npos, "the stash");
      Check(notice->text.find("20261008-120000-units") != std::string::npos, "where the copy is");
      Check(notice->text.find("7.TSV") != std::string::npos && notice->text.find("9.TSV") == std::string::npos,
            "a save left alone with unknown units listed; an unreadable one not");
    }
    const auto spanish = [](const std::string& s) { return s == "OK" ? std::string("Aceptar") : s; };
    const auto translated = SaveUnitsNotice(report, spanish);
    Check(translated && translated->buttons[0] == "Aceptar", "texts go through the translation");
  }

  if (failures) return EXIT_FAILURE;
  std::puts("save_units_test: ok");
  return EXIT_SUCCESS;
}
