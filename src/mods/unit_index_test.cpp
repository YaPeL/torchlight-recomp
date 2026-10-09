// The unit index file (unit_index.h) on synthetic indexes: the layout byte for byte, reading back
// what is written, refusing short or long files, the groups by folder, the entry-by-entry
// comparison, merging mods' units the way PC builds its table, and checking what the game kept
// after loading one.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "mods/unit_index.h"

using namespace torchlight::mods;

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
  if (!ok) {
    ++failures;
    std::fprintf(stderr, "FAIL: %s\n", what);
  }
}

UnitEntry Unit(int64_t guid, std::u16string name, std::u16string file, uint32_t level = 1,
               bool equipment = false, std::u16string type = u"ITEM") {
  UnitEntry e;
  e.guid = guid;
  e.name = std::move(name);
  e.file = std::move(file);
  e.equipment = equipment;
  e.level = level;
  e.min_level = 2;
  e.max_level = 3;
  e.rarity = 4;
  e.rarity_hardcore = 5;
  e.unit_type = std::move(type);
  return e;
}

UnitIndex Sample() {
  UnitIndex index;
  index.groups[0] = {Unit(-2, u"SWORD", u"MEDIA/UNITS/ITEMS/SWORD.DAT", 7, true),
                     Unit(11, u"", u"MEDIA/UNITS/ITEMS/BASE.DAT")};
  index.groups[1] = {Unit(21, u"BAT", u"MEDIA/UNITS/MONSTERS/BAT/BAT.DAT", 1, false, u"Monster")};
  index.groups[3] = {Unit(31, u"BARREL", u"MEDIA/UNITS/PROPS/BARREL.DAT")};
  return index;
}
}  // namespace

int main() {
  // Layout: one entry spelled out byte by byte.
  {
    UnitIndex one;
    one.groups[2] = {Unit(0x0102030405060708, u"AB", u"F", 9, true, u"P")};
    const std::vector<uint8_t> bytes = WriteUnitIndex(one);
    const std::vector<uint8_t> expected = {
        0, 0, 0, 0,  0, 0, 0, 0,  1, 0, 0, 0,                   // groups 0 and 1 empty, group 2: 1
        8, 7, 6, 5, 4, 3, 2, 1,                                  // GUID, little-endian
        2, 0, 'A', 0, 'B', 0,                                    // NAME
        1, 0, 'F', 0,                                            // file
        1,                                                       // equipment
        9, 0, 0, 0,  2, 0, 0, 0,  3, 0, 0, 0,  4, 0, 0, 0,  5, 0, 0, 0,
        1, 0, 'P', 0,                                            // UNITTYPE
        0, 0, 0, 0};                                             // group 3 empty
    Check(bytes == expected, "layout");
  }
  // Round trip and refusals.
  {
    const UnitIndex index = Sample();
    const std::vector<uint8_t> bytes = WriteUnitIndex(index);
    std::string error;
    const auto back = ParseUnitIndex(bytes, &error);
    Check(back && back->groups == index.groups && back->size() == 4, "round trip");
    std::vector<uint8_t> shorter(bytes.begin(), bytes.end() - 1);
    Check(!ParseUnitIndex(shorter, &error), "truncated file refused");
    std::vector<uint8_t> longer = bytes;
    longer.push_back(0);
    Check(!ParseUnitIndex(longer, &error) && error.find("bytes left") != std::string::npos,
          "extra bytes refused");
    std::vector<uint8_t> huge = bytes;
    huge[0] = huge[1] = huge[2] = 0xFF;
    Check(!ParseUnitIndex(huge, &error), "implausible count refused");
  }
  // Groups by folder.
  {
    Check(GroupOfUnitFile(u"media\\units\\items\\gold\\gold.dat") == UnitGroup::kItems, "items, any case");
    Check(GroupOfUnitFile(u"MEDIA/UNITS/MONSTERS/X.DAT") == UnitGroup::kMonsters, "monsters");
    Check(GroupOfUnitFile(u"MEDIA/UNITS/PLAYERS/X.DAT") == UnitGroup::kPlayers, "players");
    Check(GroupOfUnitFile(u"MEDIA/UNITS/PROPS/X.DAT") == UnitGroup::kProps, "props");
    Check(!GroupOfUnitFile(u"MEDIA/UNITS/OTHER/X.DAT") && !GroupOfUnitFile(u"MEDIA/UNITS/ITEMS/"),
          "outside the four folders: none");
  }
  // Comparison: order does not matter, every field does.
  {
    const UnitIndex a = Sample();
    UnitIndex b = Sample();
    std::swap(b.groups[0][0], b.groups[0][1]);
    b.groups[0][0].file = u"media\\units\\items\\base.dat";
    Check(CompareUnitIndexes(a, b).empty(), "same entries in another order, path spelled otherwise");
    b.groups[1][0].unit_type = u"MONSTER";
    b.groups[3].clear();
    b.groups[3].push_back(Unit(99, u"NEW", u"MEDIA/UNITS/PROPS/NEW.DAT"));
    const auto diff = CompareUnitIndexes(a, b);
    Check(diff.size() == 3, "type change, a missing and an extra entry");
    Check(CompareUnitIndexes(a, b, 1).size() == 2, "limit and the count of the rest");
  }
  // Merge: same file replaces in place, new GUID goes to its group's end, a GUID clash goes to the
  // higher priority, units outside the folders are left out.
  {
    const UnitIndex base = Sample();
    std::vector<std::u16string> skipped;
    std::vector<UnitEntry> mods = {
        Unit(-2, u"SWORD", u"media/units/items/sword.dat", 40, true),    // overrides the sword
        Unit(500, u"AXE", u"MEDIA/UNITS/ITEMS/MOD/AXE.DAT", 3, true),    // new
        Unit(600, u"NEWBAT", u"MEDIA/UNITS/MONSTERS/MOD/BAT2.DAT"),      // new monster
        Unit(500, u"AXE2", u"MEDIA/UNITS/ITEMS/MOD2/AXE.DAT", 5, true),  // same GUID, later: wins
        Unit(700, u"X", u"MEDIA/OTHER/X.DAT"),                           // outside
    };
    const UnitIndex merged = MergeUnitIndex(base, mods, &skipped);
    Check(merged.groups[0].size() == 3, "items: two base entries and one new");
    Check(merged.groups[0][0].level == 40 && merged.groups[0][0].file == u"media/units/items/sword.dat",
          "the overriding file takes the base entry's place");
    Check(merged.groups[0][2].name == u"AXE2", "a GUID clash: the later (winning) unit's entry");
    Check(merged.groups[1].size() == 2 && merged.groups[1][1].guid == 600, "new monster at its group's end");
    Check(skipped.size() == 1 && skipped[0] == u"MEDIA/OTHER/X.DAT", "outside: skipped and listed");
    Check(MergeUnitIndex(base, {}, nullptr).groups == base.groups, "no mods: the base unchanged");
  }

  // What the game should hold after loading an index: one entry per GUID, and the verdict on the
  // size of its GUID map.
  {
    UnitIndex index = Sample();
    Check(LoadedUnitCount(index) == 3, "four entries, one without a NAME: three kept");
    index.groups[3].push_back(Unit(-2, u"SWORD_PROP", u"MEDIA/UNITS/PROPS/SWORD.DAT"));
    Check(LoadedUnitCount(index) == 3, "a GUID repeated in another group counts once");
    index.groups[3].push_back(Unit(11, u"BASE_PROP", u"MEDIA/UNITS/PROPS/BASE.DAT"));
    Check(LoadedUnitCount(index) == 4, "a GUID kept once one of its entries has a NAME");
    Check(LoadedUnitCount(UnitIndex{}) == 0, "an empty index");
    Check(CheckLoadedIndex(4, 4) == LoadedIndex::kComplete, "all loaded");
    Check(CheckLoadedIndex(0, 4) == LoadedIndex::kEmpty, "nothing loaded: empty");
    Check(CheckLoadedIndex(3, 4) == LoadedIndex::kIncomplete, "fewer: incomplete");
    Check(CheckLoadedIndex(5, 4) == LoadedIndex::kMore, "more than the file holds");
    Check(CheckLoadedIndex(0, 0) == LoadedIndex::kComplete, "an empty file loaded as empty");
  }

  if (failures) return EXIT_FAILURE;
  std::puts("unit_index_test: ok");
  return EXIT_SUCCESS;
}
