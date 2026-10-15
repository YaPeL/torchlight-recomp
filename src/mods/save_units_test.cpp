// Taking unknown units out of saves (save_units.h) on the synthetic PC save of the import tests
// (tests/save_import/fixtures, made by the Python tool; no game data): an unknown item removed and
// the file still read back, and every safeguard leaving the save alone: an incomplete or broken
// index, most units unknown, the character's own class unknown, an unreadable file; and, after the
// game loaded its index, the units the check counted on that it does not hold, the saves holding
// them (copied, saving off for the session) and their notice.
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>

#include "game_menu/menu_strings.h"
#include "mods/save_units.h"
#include "mods/saved_mod_list.h"
#include "save_import/digest.h"
#include "mods/save_units_notice.h"
#include "mods/settled_saves.h"
#include "save_import/schema.h"

using namespace torchlight::mods;
namespace si = torchlight::save_import;

namespace {

std::set<std::string> g_notice_texts;  // every English text the notices asked for
std::string English(const std::string& text) {
  g_notice_texts.insert(text);
  return text;
}
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
    si::Bytes now_bytes;
    {
      // Closed before the folder is removed: Windows does not delete a file that is open.
      std::ifstream back(saves / "0.TSV", std::ios::binary);
      now_bytes.assign(std::istreambuf_iterator<char>(back), {});
    }
    auto reparsed = Parse(*schema, now_bytes);
    Check(reparsed && !UnitGuids(*reparsed).contains(kMod), "the file on disk no longer has the item");
    Check(!fs::exists(saves / "0.TSV.units.tmp"), "no temporary file left");
    const auto again = ProtectSaves(root, "58410A7E", *schema, MakeKnownUnits(IndexWith(base), {}, true), now, log);
    Check(again.changed.empty() && again.backup.empty(), "a second pass finds nothing and backs nothing up");
    std::error_code ec;
    fs::remove_all(root, ec);
  }

  // The player's notice: nothing to tell, the removed items with the backup, risky saves.
  {
    const auto english = English;
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
      Check(notice->text.find("Removed 3 items from mods") != std::string::npos, "the total");
      Check(notice->text.find("  Ronan: 2\n") != std::string::npos, "per character");
      Check(notice->text.find("  Shared stash: 1\n") != std::string::npos, "the stash");
      Check(notice->text.find("Backup: save-backups/20261008-120000-units\n") != std::string::npos,
            "where the copy is, from the user data folder on");
      Check(notice->text.find("7.TSV") != std::string::npos && notice->text.find("9.TSV") == std::string::npos,
            "a save left alone with unknown units listed; an unreadable one not");
    }
    const auto spanish = [](const std::string& s) { return s == "OK" ? std::string("Aceptar") : s; };
    const auto translated = SaveUnitsNotice(report, spanish);
    Check(translated && translated->buttons[0] == "Aceptar", "texts go through the translation");
  }

  // After the game loaded its index: the units the check counted on that it does not hold, and the
  // notice that says so (alone, or after what was removed).
  {
    const KnownUnits assumed = MakeKnownUnits(IndexWith(all), {}, true);
    Check(UnitsNotLoaded(assumed, assumed.guids).empty(), "everything loaded: nothing missing");
    std::unordered_set<int64_t> loaded = assumed.guids;
    loaded.erase(kMod);
    const auto missing = UnitsNotLoaded(assumed, loaded);
    Check(missing.size() == 1 && missing[0] == kMod, "the mod's unit missing from the loaded index");
    const auto none = UnitsNotLoaded(assumed, {});
    Check(none.size() == assumed.guids.size() && std::is_sorted(none.begin(), none.end()),
          "nothing loaded: every unit missing, sorted");

    const auto english = English;
    SaveUnitsReport only;
    only.units_not_loaded = 3;
    const auto notice = SaveUnitsNotice(only, english);
    Check(notice && notice->title == "Mod items not loaded" &&
              notice->text.find("3 kinds of mod items did not load this time") != std::string::npos,
          "units not loaded alone: their own title and text");
    SaveUnitsReport both;
    both.changed.push_back({"/saves/a/4.TSV", "Ronan", {{"player/items/item", "", 1}}});
    both.units_not_loaded = 2;
    const auto with_removed = SaveUnitsNotice(both, english);
    Check(with_removed && with_removed->title == "Mod items removed" &&
              with_removed->text.find("2 kinds") != std::string::npos &&
              with_removed->text.find("Ronan: 1\n") != std::string::npos,
          "with items removed: the removal's title, both texts");
  }

  // Units the game did not load after the check: the saves holding them found (read only), every
  // save copied, saving off for the session, and nothing in the saves changed; the notice says so.
  {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() /
        ("tl_not_loaded_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path saves = root / "B13EBABEBABEBABE" / "58410A7E" / "00000001" / "torchlight.sav";
    fs::create_directories(saves);
    auto write = [](const fs::path& p, const si::Bytes& b) {
      std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
    };
    auto read = [](const fs::path& p) {
      std::ifstream in(p, std::ios::binary);
      return si::Bytes(std::istreambuf_iterator<char>(in), {});
    };
    si::SaveError e;
    const auto clean = si::ReadPc(*schema, Fixture("pc_save.svt"), e);
    write(saves / "4.TSV", x360);                       // holds kMod
    write(saves / "1.tsv", si::Write360(*schema, *clean));  // does not
    write(saves / "sharedstash.bin", Fixture("pc_stash.expected.bin"));
    const auto now = std::chrono::system_clock::now();
    std::vector<std::string> lines;
    auto log = [&](const std::string& l) { lines.push_back(l); };

    SaveUnitsReport unrelated;
    ProtectFromUnitsNotLoaded(root, "58410A7E", *schema, {0x0123}, now, log, unrelated);
    Check(!unrelated.saving_blocked && unrelated.holding_not_loaded.empty() && !fs::exists(root / "save-backups"),
          "units no save holds: saving stays on, no copy");

    const auto holding = SavesHoldingUnits(root, "58410A7E", *schema, {kMod});
    Check(holding.size() == 1 && holding[0].path.filename() == "4.TSV" && !holding[0].owner.empty(),
          "the character holding the unit found, with its owner");
    const si::Bytes before = read(saves / "4.TSV");
    SaveUnitsReport report;
    ProtectFromUnitsNotLoaded(root, "58410A7E", *schema, {kMod}, now, log, report);
    Check(report.saving_blocked && report.holding_not_loaded.size() == 1 &&
              report.holding_not_loaded[0].ends_with("(4.TSV)"),
          "saving off for the session, the save listed as owner (file)");
    Check(report.not_loaded_backup.filename().string().ends_with("-units-not-loaded") &&
              fs::exists(report.not_loaded_backup / "B13EBABEBABEBABE" / "58410A7E" / "00000001" / "torchlight.sav" /
                         "4.TSV"),
          "every save copied first, under its own reason");
    Check(read(saves / "4.TSV") == before && report.changed.empty(), "the saves themselves untouched");

    // A stash holding the unit is listed too, without an owner.
    auto stash = si::Read360Stash(*schema, Fixture("pc_stash.expected.bin"), e);
    si::Node* items = stash ? stash->tree.Find("items") : nullptr;
    si::Node* guid = items && !items->items.empty() ? items->items.front().Find("unit_guid") : nullptr;
    Check(guid != nullptr, "the stash fixture has an item");
    if (guid) {
      guid->number = static_cast<uint64_t>(kMod);
      write(saves / "sharedstash.bin", si::Write360Stash(*schema, *stash));
      const auto both = SavesHoldingUnits(root, "58410A7E", *schema, {kMod});
      Check(both.size() == 2 && both[1].path.filename() == "sharedstash.bin" && both[1].owner.empty(),
            "the stash holding the unit listed, with no owner");
    }

    const auto english = English;
    report.units_not_loaded = 1;
    report.holding_not_loaded.push_back("");
    const auto notice = SaveUnitsNotice(report, english);
    Check(notice && notice->title == "Saving is off this session" &&
              notice->text.find("NOTHING WILL BE SAVED THIS SESSION") != std::string::npos &&
              notice->text.find("Quit the game and see the log.") != std::string::npos &&
              notice->text.find("(4.TSV)") != std::string::npos &&
              notice->text.find("Shared stash") != std::string::npos &&
              notice->text.find("-units-not-loaded") != std::string::npos,
          "the notice: nothing saved, which saves, where the copy is");
    report.not_loaded_backup.clear();
    const auto no_copy = SaveUnitsNotice(report, english);
    Check(no_copy && no_copy->text.find("could not be copied") != std::string::npos, "the notice when no copy was made");
    SaveUnitsReport not_held;
    not_held.units_not_loaded = 2;
    const auto plain = SaveUnitsNotice(not_held, english);
    Check(plain && plain->title == "Mod items not loaded" &&
              plain->text.find("No save has them.") != std::string::npos &&
              plain->text.find("NOTHING WILL BE SAVED") == std::string::npos,
          "units not loaded that no save carries: told, saving not off");
    std::error_code ec;
    fs::remove_all(root, ec);
  }

  // Whose units: the player's are removed or protect the saves; a saved level's creatures (Tarn
  // the Merchant, whose GUID a mod replaced), a quest's units and guids2/ are only logged.
  {
    Check(PlayerOwnedUnitRef("player/items/item/unit_guid", false) && PlayerOwnedUnitRef("player/unit_guid", false) &&
              PlayerOwnedUnitRef("pets/unit/unit_guid", false) &&
              PlayerOwnedUnitRef("levels/level/items/item/unit_guid", false) &&
              PlayerOwnedUnitRef("items/item/unit_guid", true),
          "the player's: character, items, pet, items on a level's floor, a stash");
    Check(!PlayerOwnedUnitRef("levels/level/units/unit/unit_guid", false) &&
              !PlayerOwnedUnitRef("levels/level/units/unit/items/item/unit_guid", false) &&
              !PlayerOwnedUnitRef("quests/active/quest/giver_unit_guid", false) &&
              !PlayerOwnedUnitRef("guids2/g", false),
          "not the player's: a level's creatures and their items, quests, guids2");
    Check(PlayerOwnedUnitRef("something/new/unit_guid", false), "anything else: taken as the player's");

    si::SaveError e;
    auto level = si::ReadPc(*schema, Fixture("pc_save.svt"), e);
    si::Node* levels = level ? level->tree.Find("levels") : nullptr;
    si::Node* units = levels && !levels->items.empty() ? levels->items.front().Find("units") : nullptr;
    si::Node* creature = units && !units->items.empty() ? units->items.front().Find("unit_guid") : nullptr;
    Check(creature != nullptr, "the fixture has a creature in a saved level");
    if (creature) {
      const int64_t kLevel = 0x0777000000000002;  // a level creature whose GUID a mod replaced
      creature->number = static_cast<uint64_t>(kLevel);
      si::Bytes with_level = si::Write360(*schema, *level);
      auto p = Parse(*schema, with_level);
      std::set<int64_t> known_set = UnitGuids(*p);
      known_set.erase(kLevel);
      const auto only_level = RemoveUnknownUnits(*p, MakeKnownUnits(IndexWith(known_set), {}, true));
      Check(only_level.result == UnitCheck::Result::kClean && only_level.removed.empty() &&
                only_level.kept_level.size() == 1 && only_level.kept_level[0].guid == kLevel &&
                only_level.kept_level[0].path == "levels/level/units/unit/unit_guid",
            "an unknown level creature: left in the save and listed, nothing removed");

      PlayerItem(*level)->Find("unit_guid")->number = static_cast<uint64_t>(kMod);
      si::Bytes both_unknown = si::Write360(*schema, *level);
      auto q = Parse(*schema, both_unknown);
      known_set = UnitGuids(*q);
      known_set.erase(kLevel);
      known_set.erase(kMod);
      const auto mixed = RemoveUnknownUnits(*q, MakeKnownUnits(IndexWith(known_set), {}, true));
      Check(mixed.result == UnitCheck::Result::kRemoved && mixed.removed.size() == 1 &&
                mixed.removed[0].guid == kMod && mixed.kept_level.size() == 1,
            "with the player's item unknown too: the item removed, the creature kept");

      namespace fs = std::filesystem;
      const fs::path root = fs::temp_directory_path() /
          ("tl_level_units_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
      const fs::path saves = root / "B13EBABEBABEBABE" / "58410A7E" / "00000001" / "torchlight.sav";
      fs::create_directories(saves);
      std::ofstream(saves / "0.TSV", std::ios::binary)
          .write(reinterpret_cast<const char*>(with_level.data()), std::streamsize(with_level.size()));
      Check(SavesHoldingUnits(root, "58410A7E", *schema, {kLevel}).empty(),
            "a save holding only a level creature the game did not load: not holding, saving stays on");
      SaveUnitsReport report;
      ProtectFromUnitsNotLoaded(root, "58410A7E", *schema, {kLevel}, std::chrono::system_clock::now(),
                                [](const std::string&) {}, report);
      Check(!report.saving_blocked && !fs::exists(root / "save-backups"), "no copy, saving on");
      std::error_code ignored;
      fs::remove_all(root, ignored);  // error_code: a failed cleanup must not throw (Windows)
    }
  }

  // Expected units with mods: merged as the index builder merges, so a mod's unit at a base
  // unit's path replaces that GUID (JCC - Map's Tarn the Merchant) instead of adding to it.
  {
    UnitIndex base;  // real base entries have files: the merge drops entries without one
    const std::u16string files[] = {u"MEDIA/UNITS/ITEMS/A.DAT", u"MEDIA/UNITS/MONSTERS/MERCHANT/MERCHANT_GOODS.DAT",
                                    u"MEDIA/UNITS/PLAYERS/P.DAT", u"MEDIA/UNITS/PROPS/C.DAT"};
    const int64_t guids[] = {1, 10, 3, 4};
    for (size_t g = 0; g < base.groups.size(); ++g) {
      UnitEntry e;
      e.guid = guids[g];
      e.file = files[g];
      base.groups[g].push_back(e);
    }
    UnitEntry replaced;
    replaced.guid = 11;
    replaced.file = u"MEDIA/UNITS/MONSTERS/MERCHANT/MERCHANT_GOODS.DAT";
    UnitEntry added;
    added.guid = 12;
    added.file = u"MEDIA/UNITS/ITEMS/NEW.DAT";
    const KnownUnits expected = MakeExpectedUnits(base, std::vector<UnitEntry>{replaced, added});
    Check(expected.complete && expected.guids.contains(11) && expected.guids.contains(12) &&
              !expected.guids.contains(10) && expected.guids.contains(1),
          "a mod's unit at a base path replaces the base GUID; a new path adds one");
    const KnownUnits unread = MakeExpectedUnits(base, std::nullopt, "a mod unreadable");
    Check(!unread.complete && unread.incomplete_why == "a mod unreadable", "mods' units not all read: incomplete");
  }

  // Repair at load (docs/mods.md 7d): a character saved by the Xbox writer's bug (every mod name's
  // length written as 0) is found, repaired with the mods' names and written back with its digest,
  // after a backup.
  {
    namespace fs = std::filesystem;
    const std::vector<std::u16string> names = {u"JCC - Map", u"Another Mod"};
    auto good_parsed = si::ReadPc(*schema, Fixture("pc_save.svt"), save_error);
    si::Node* list = good_parsed->tree.Find("player")->Find("names");
    Check(list != nullptr, "the player has a mod list");
    list->items.clear();
    for (const auto& n : names) {
      si::Node name;
      name.kind = si::Node::Kind::kText;
      name.text = n;
      list->items.push_back(name);
    }
    const si::Bytes good = si::Write360(*schema, *good_parsed);
    // The list in the body: the count, then each name's length and units (EncodeSavedModList less
    // its leading u32); damage it as the bug does.
    std::vector<uint8_t> correct = EncodeSavedModList(0, names, false);
    correct.erase(correct.begin(), correct.begin() + 4);
    std::vector<uint8_t> broken = EncodeSavedModList(0, names, true);
    broken.erase(broken.begin(), broken.begin() + 4);
    std::vector<uint8_t> damaged(good.begin(), good.end() - 32);
    const auto at = std::search(damaged.begin(), damaged.end(), correct.begin(), correct.end());
    Check(at != damaged.end(), "the list is in the body");
    if (at != damaged.end()) std::copy(broken.begin(), broken.end(), at);
    const si::Sha256Digest digest = si::Sha256(damaged);
    damaged.insert(damaged.end(), digest.begin(), digest.end());
    {
      std::span<const uint8_t> body;
      si::SaveError e;
      Check(si::Split360(damaged, body, e) && !si::ParseBody(*schema, *schema->root(), body, si::Endian::kBig, e),
            "the damaged save does not read");
    }
    const fs::path root = fs::temp_directory_path() / "save_units_test_modlist";
    std::error_code cleanup;
    fs::remove_all(root, cleanup);
    const fs::path file = root / "B13EBABEBABEBABE" / "58410A7E" / "00000001" / "torchlight.sav" / "1.TSV";
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char*>(damaged.data()),
                                                 static_cast<std::streamsize>(damaged.size()));
    std::vector<std::string> lines;
    const auto now = std::chrono::system_clock::now();
    // Without the names, nothing can be repaired: left alone, nothing written.
    auto none = RepairModListSaves(root, "58410A7E", *schema, {u"Unrelated"}, now,
                                   [&](const std::string& l) { lines.push_back(l); });
    Check(none.repaired.empty() && none.left_alone.size() == 1 && none.backup.empty(),
          "unknown names: left alone, no backup");
    auto report = RepairModListSaves(root, "58410A7E", *schema, {u"Unrelated", u"JCC - Map", u"Another Mod", u"JCC"},
                                     now, [&](const std::string& l) { lines.push_back(l); });
    Check(report.repaired.size() == 1 && !report.failed && fs::exists(report.backup), "repaired after a backup");
    {
      // Closed before the folder is removed: Windows does not delete a file that is open.
      std::ifstream in(file, std::ios::binary);
      const si::Bytes after((std::istreambuf_iterator<char>(in)), {});
      Check(after == good, "the repaired file is the save as it should have been written");
    }
    // A good save is not touched.
    auto again = RepairModListSaves(root, "58410A7E", *schema, {u"JCC - Map", u"Another Mod"}, now, nullptr);
    Check(again.repaired.empty() && again.left_alone.empty(), "a save that reads is left as it is");
    std::error_code ec;
    fs::remove_all(root, ec);
  }

  // Saves settled at a start are not read again at the next while neither they nor the context
  // changed (settled_saves.h); a save that changes is read again, and an unreadable one is logged
  // once, not at every start.
  {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "save_units_test_settled";
    std::error_code cleanup;
    fs::remove_all(root, cleanup);
    const fs::path saves = root / "B13EBABEBABEBABE" / "58410A7E" / "00000001" / "torchlight.sav";
    fs::create_directories(saves);
    auto write = [](const fs::path& p, const si::Bytes& b) {
      std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
    };
    const KnownUnits known = MakeKnownUnits(IndexWith(base), {}, true);
    si::Bytes clean_bytes;
    {
      auto clean = Parse(*schema, x360);
      RemoveUnknownUnits(*clean, known);
      clean_bytes = si::Write360(*schema, *clean);
    }
    write(saves / "1.tsv", clean_bytes);                          // clean
    write(saves / "2.tsv", si::Bytes{'n', 'o', 't', ' ', 'a', ' ', 's', 'a', 'v', 'e'});  // unreadable
    write(saves / "4.TSV", x360);                                 // holds kMod: changed at the first start
    const fs::path memory = root / "cache" / "settled-saves.txt";
    const std::string context = SettledSaves::Context(known, {u"Some Mod"}, {});
    const auto now = std::chrono::system_clock::now();
    struct Start {
      SaveUnitsReport report;
      std::vector<std::string> lines;
    };
    // One start: what was settled before is skipped, and what is settled now is remembered.
    auto start = [&](const KnownUnits& units, const std::string& ctx) {
      Start out;
      const SettledSaves last = SettledSaves::Load(memory, root, ctx);
      SettledSaves next(root, ctx);
      out.report = ProtectSaves(root, "58410A7E", *schema, units, now,
                                [&](const std::string& l) { out.lines.push_back(l); },
                                [&](const fs::path& p) {
                                  if (!last.Unchanged(p)) return false;
                                  next.Settle(p);
                                  return true;
                                });
      for (const auto& p : out.report.settled) next.Settle(p);
      Check(next.Store(memory), "the settled saves are written");
      return out;
    };
    auto mentions = [](const Start& s, const std::string& part) {
      return std::any_of(s.lines.begin(), s.lines.end(), [&](const std::string& l) { return l.find(part) != std::string::npos; });
    };
    auto settled = [](const Start& s, const char* name) {
      return std::any_of(s.report.settled.begin(), s.report.settled.end(),
                         [&](const fs::path& p) { return p.filename() == name; });
    };

    const Start first = start(known, context);
    Check(first.report.skipped == 0 && first.report.changed.size() == 1 && settled(first, "1.tsv") &&
              settled(first, "2.tsv") && !settled(first, "4.TSV") && mentions(first, "unreadable"),
          "first start: everything read, the clean and the unreadable settled, the changed one not");
    const Start second = start(known, context);
    Check(second.report.skipped == 2 && settled(second, "4.TSV") && second.report.changed.empty() &&
              !mentions(second, "unreadable"),
          "second start: the settled two not read (the unreadable one not logged again); the save written "
          "at the first is read, now settled");
    const Start third = start(known, context);
    Check(third.report.skipped == 3 && third.lines.empty(), "third start: nothing read, nothing logged");

    // The mod list repair skips them too: the unreadable save it cannot repair is not tried, nor
    // logged, again.
    {
      const SettledSaves last = SettledSaves::Load(memory, root, context);
      std::vector<std::string> lines;
      const auto repair = RepairModListSaves(root, "58410A7E", *schema, {u"Some Mod"}, now,
                                             [&](const std::string& l) { lines.push_back(l); },
                                             [&](const fs::path& p) { return last.Unchanged(p); });
      Check(repair.skipped == 3 && repair.left_alone.empty() && lines.empty(), "the repair skips the settled saves");
      const auto unskipped = RepairModListSaves(root, "58410A7E", *schema, {u"Some Mod"}, now, nullptr);
      Check(unskipped.skipped == 0, "without the memory it reads them all");
    }
    // A save that changes is read again.
    fs::last_write_time(saves / "1.tsv", fs::last_write_time(saves / "1.tsv") + std::chrono::seconds(2));
    const Start touched = start(known, context);
    Check(touched.report.skipped == 2 && settled(touched, "1.tsv"), "a save written since is read again");
    write(saves / "2.tsv", si::Bytes{'n', 'o', 't', ' ', 'a', ' ', 's', 'a', 'v', 'e', '!'});
    const Start grown = start(known, context);
    Check(grown.report.skipped == 2 && mentions(grown, "unreadable"), "an unreadable save that changed is told again");

    // Another context (other units, other names a broken list may hold) forgets every save.
    Check(SettledSaves::Context(known, {u"Some Mod"}, {}) == SettledSaves::Context(known, {u"Some Mod", u"Some Mod"}, {}) &&
              SettledSaves::Context(known, {u"Other Mod"}, {}) != context &&
              SettledSaves::Context(MakeKnownUnits(IndexWith(all), {}, true), {u"Some Mod"}, {}) != context,
          "the context follows the units and the names");
    const Start other = start(MakeKnownUnits(IndexWith(all), {}, true),
                             SettledSaves::Context(MakeKnownUnits(IndexWith(all), {}, true), {u"Some Mod"}, {}));
    Check(other.report.skipped == 0 && mentions(other, "unreadable"), "another context: every save read");

    // A save left alone with an unknown unit (here because the known units are incomplete) is not
    // settled: it is checked, and told, at every start.
    write(saves / "5.TSV", x360);  // holds kMod
    KnownUnits incomplete = known;
    incomplete.complete = false;
    incomplete.incomplete_why = "test";
    const std::string incomplete_context = SettledSaves::Context(incomplete, {u"Some Mod"}, {});
    Check(incomplete_context != context, "an incomplete set is another context");
    const Start left = start(incomplete, incomplete_context);
    Check(!settled(left, "5.TSV") && settled(left, "1.tsv") && settled(left, "2.tsv") &&
              left.report.changed.empty() && mentions(left, "5.TSV"),
          "a save left alone with an unknown unit: not settled; the others are");
    const Start again = start(incomplete, incomplete_context);
    Check(again.report.skipped == 3 && mentions(again, "5.TSV"), "so it is read, and told, again");

    // A memory file that does not read, or of another format, is no memory.
    std::ofstream(memory, std::ios::binary) << "garbage\n";
    Check(SettledSaves::Load(memory, root, context).size() == 0, "an unreadable memory: nothing settled");
    fs::remove_all(root, cleanup);
  }

  // Characters last saved with other mods than the active ones (the game's main menu warning): told
  // and not settled; the lists compared in count and by name at each place, as the game does.
  {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "save_units_test_other_mods";
    std::error_code cleanup;
    fs::remove_all(root, cleanup);
    const fs::path saves = root / "B13EBABEBABEBABE" / "58410A7E" / "00000001" / "torchlight.sav";
    fs::create_directories(saves);
    const KnownUnits known = MakeKnownUnits(IndexWith(base), {}, true);
    auto parsed = Parse(*schema, x360);
    RemoveUnknownUnits(*parsed, known);
    si::Node* names = parsed->tree.Find("player")->Find("names");
    Check(names && names->kind == si::Node::Kind::kList, "player/names is a list");
    names->items.clear();
    for (const char16_t* name : {u"Mod A", u"Mod B"}) {
      si::Node text;
      text.kind = si::Node::Kind::kText;
      text.text = name;
      names->items.push_back(text);
    }
    const si::Bytes bytes = si::Write360(*schema, *parsed);
    std::ofstream(saves / "3.TSV", std::ios::binary)
        .write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    const auto check = CheckCharacterFile(*schema, bytes, known);
    Check(check.mods && *check.mods == std::vector<std::u16string>{u"Mod A", u"Mod B"}, "the saved list read");
    const auto now = std::chrono::system_clock::now();
    auto pass = [&](const std::vector<std::u16string>* active) {
      return ProtectSaves(root, "58410A7E", *schema, known, now, {}, {}, active);
    };
    const std::vector<std::u16string> same = {u"Mod A", u"Mod B"}, reordered = {u"Mod B", u"Mod A"},
                                      fewer = {u"Mod A"}, none;
    const auto equal = pass(&same);
    Check(equal.other_mods.empty() && equal.settled.size() == 1, "the same list: nothing told, settled");
    Check(pass(nullptr).other_mods.empty(), "no active list given: not compared");
    for (const auto* active : {&reordered, &fewer, &none}) {
      const auto other = pass(active);
      Check(other.other_mods.size() == 1 && other.other_mods[0].saved == same && other.settled.empty() &&
                !other.other_mods[0].owner.empty(),
            "another order, fewer mods or none: told, with the character's name, and not settled");
    }
    fs::remove_all(root, cleanup);

    // The context changes with the active list, its order too.
    Check(SettledSaves::Context(known, {}, same) != SettledSaves::Context(known, {}, reordered) &&
              SettledSaves::Context(known, {}, same) != SettledSaves::Context(known, {}, {}),
          "the active mods are part of the context, in order");

    SaveUnitsReport report;
    report.other_mods.push_back({"/saves/a/3.TSV", "Ronan", same});
    report.other_mods.push_back({"/saves/a/5.TSV", "", {}});
    const auto notice = SaveUnitsNotice(report, English);
    Check(notice && notice->title == "Mods changed" &&
              notice->text.find("These characters were last saved with other mods:\n  Ronan\n  5.TSV\n") !=
                  std::string::npos &&
              notice->text.find("at your own risk") != std::string::npos,
          "the notice: the characters (a file name without one) and the risk");
    report.failed = true;
    const auto both = SaveUnitsNotice(report, English);
    Check(both && both->title == "Mod items removed" && both->text.find("Ronan") != std::string::npos,
          "with another notice: added to it");
  }

  // A failure, and every notice text in each language of the translations, its {words} kept (a text
  // of {words} only, like "{name}: {count}", may stay as it is).
  {
    SaveUnitsReport report;
    report.failed = true;
    const auto notice = SaveUnitsNotice(report, English);
    Check(notice && notice->text.find("Some saves could not be backed up or changed") != std::string::npos,
          "a failure told");
    torchlight::game_menu::MenuStrings strings;
    std::vector<std::string> warnings;
    std::string error;
    Check(strings.Load(TL_IMPORT_STRINGS_FILE, warnings, error) && warnings.empty(), "the translations load");
    for (const char* language : {"de", "fr", "es"}) {
      for (const std::string& english : g_notice_texts) {
        const std::string translated = strings.Translate(language, english);
        bool words_only = true;
        for (size_t i = 0, depth = 0; i < english.size(); ++i) {
          if (english[i] == '{') ++depth;
          if (depth == 0 && std::isalpha(static_cast<unsigned char>(english[i])) && english != "OK") words_only = false;
          if (english[i] == '}') --depth;
        }
        Check(translated != english || words_only || english == "OK",
              std::string("missing ") + language + " translation: " + english);
        for (size_t at = english.find('{'); at != std::string::npos; at = english.find('{', at + 1)) {
          const std::string word = english.substr(at, english.find('}', at) - at + 1);
          Check(translated.find(word) != std::string::npos, std::string(language) + " drops " + word);
        }
      }
    }
  }

  if (failures) return EXIT_FAILURE;
  std::puts("save_units_test: ok");
  return EXIT_SUCCESS;
}
