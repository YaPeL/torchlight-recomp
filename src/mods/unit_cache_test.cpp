// The unit index cache (unit_cache.h) on synthetic mods and files: which mod files count, their
// order by priority, the key changing with the mods, the base and nothing else, the pak's
// identity, and storing, finding, discarding and pruning cached indexes.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "mods/unit_cache.h"
#include "save_import/pak.h"

namespace fs = std::filesystem;
using namespace torchlight::mods;

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
  if (!ok) {
    ++failures;
    std::fprintf(stderr, "FAIL: %s\n", what);
  }
}
void Write(const fs::path& path, const std::string& text) {
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << text;
}
ModPlan Plan(std::initializer_list<std::pair<const char*, int32_t>> mods) {
  ModPlan plan;
  for (auto& [folder, priority] : mods) plan.mods.push_back({folder, priority});
  return plan;
}
// A zip with no entries but a given central directory (its bytes are only hashed).
std::string Zip(const std::string& central_directory) {
  std::string out = central_directory;
  const uint32_t size = static_cast<uint32_t>(central_directory.size());
  std::string eocd = {'P', 'K', 5, 6, 0, 0, 0, 0, 0, 0, 0, 0};
  for (int i = 0; i < 4; ++i) eocd.push_back(static_cast<char>(size >> (8 * i)));
  for (int i = 0; i < 4; ++i) eocd.push_back(0);  // the directory starts at offset 0
  eocd += std::string(2, '\0');
  return out + eocd;
}
}  // namespace

int main() {
  const fs::path root = fs::temp_directory_path() /
      ("tl_unit_cache_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  const fs::path mods = root / "mods";
  Write(mods / "a" / "media" / "units" / "items" / "axe.dat", "axe a");
  Write(mods / "a" / "media" / "units" / "monsters" / "bat.dat", "bat a");
  Write(mods / "a" / "media" / "units" / "items" / "axe.dat.adm", "compiled");  // not a definition
  Write(mods / "a" / "media" / "levelsets" / "x.dat", "not a unit");
  Write(mods / "b" / "MEDIA" / "Units" / "Items" / "AXE.DAT", "axe b");
  Write(mods / "c" / "media" / "units" / "items" / "cow.dat", "disabled");

  const ModPlan plan = Plan({{"a", 1}, {"b", 2}, {"c", -1}});
  const auto files = ScanModUnitFiles(mods, plan);
  Check(files.size() == 3, "unit definitions of the enabled mods only");
  Check(files.size() == 3 && files[0].mod_folder == "a" && files[0].game_path == u"MEDIA/UNITS/ITEMS/AXE.DAT" &&
            files[2].mod_folder == "b" && files[2].game_path == u"MEDIA/UNITS/ITEMS/AXE.DAT",
        "game paths upper case, in priority order");
  const auto paths = UnitPathsByPriority(files);
  Check(paths.size() == 2 && paths[0] == u"MEDIA/UNITS/MONSTERS/BAT.DAT" && paths[1] == u"MEDIA/UNITS/ITEMS/AXE.DAT",
        "a path ranks by the first mod that has it; the first mod's paths last");

  // The mods' units as the builder merges them: the first mod's file of each path, with its GUID.
  {
    const fs::path defs = root / "defs";
    Write(defs / "a" / "media" / "units" / "items" / "axe.dat", "[UNIT]\r\n<STRING>UNIT_GUID:5\r\n[/UNIT]\r\n");
    Write(defs / "a" / "media" / "units" / "monsters" / "bat.dat", "[UNIT]\r\n<STRING>UNIT_GUID:7\r\n[/UNIT]\r\n");
    Write(defs / "b" / "media" / "units" / "items" / "axe.dat", "[UNIT]\r\n<STRING>UNIT_GUID:6\r\n[/UNIT]\r\n");
    std::string why;
    const auto entries = ModUnitEntries(ScanModUnitFiles(defs, Plan({{"a", 1}, {"b", 2}})), &why);
    Check(entries && entries->size() == 2 && (*entries)[0].guid == 7 && (*entries)[1].guid == 5 &&
              (*entries)[1].file == u"MEDIA/UNITS/ITEMS/AXE.DAT",
          "one entry per path, the first mod's GUID, in merge order");
    Write(defs / "b" / "media" / "units" / "items" / "cow.dat", "[UNIT]\r\n<STRING>NAME:cow\r\n[/UNIT]\r\n");
    Check(!ModUnitEntries(ScanModUnitFiles(defs, Plan({{"a", 1}, {"b", 2}})), &why) && !why.empty(),
          "a definition without UNIT_GUID: not all known, and why");
  }

  // The key: the mods' files, the base and the version; nothing else.
  const std::string key = UnitCacheKey(files, 1);
  Check(key.size() == 16 && UnitCacheKey(files, 1) == key, "a stable 64-bit hex key");
  Check(UnitCacheKey(files, 2) != key, "another base: another key");
  auto changed = files;
  changed[0].digest ^= 1;
  Check(UnitCacheKey(changed, 1) != key, "another content: another key");
  changed = files;
  changed[0].mod_folder = "z";
  Check(UnitCacheKey(changed, 1) != key, "another mod: another key");
  Check(UnitCacheKey(ScanModUnitFiles(mods, Plan({{"b", 1}, {"a", 2}, {"c", -1}})), 1) != key,
        "another priority order: another key");

  // The pak's identity: its central directory.
  Write(root / "p1.zip", Zip("directory one"));
  Write(root / "p2.zip", Zip("directory two"));
  Write(root / "p3.zip", "not a zip at all, long enough to look for a record");
  const auto id1 = PakIdentity(root / "p1.zip");
  Check(id1 && PakIdentity(root / "p1.zip") == id1, "pak identity read");
  Check(PakIdentity(root / "p2.zip") != id1, "another directory: another identity");
  Check(!PakIdentity(root / "p3.zip") && !PakIdentity(root / "missing.zip"), "not a zip: none");

  // File names in upper case, whatever the name: the game asks for them that way and finds them
  // through a case-sensitive lookup on Linux.
  Check(UnitIndexFileName("0a1b2c3d4e5f6789") == "0A1B2C3D4E5F6789.RAW", "a key in upper case, then .RAW");
  Check(UnitIndexFileName(IncompleteUnitIndexName("0a1b")) == "0A1B-INCOMPLETE.RAW", "the incomplete name too");

  // Store, find, discard, prune.
  const fs::path cache = root / "cache";
  UnitIndex index;
  index.groups[0].push_back({});
  index.groups[0][0].file = u"MEDIA/UNITS/ITEMS/AXE.DAT";
  std::string error, log;
  Check(!FindCachedUnitIndex(cache, key, &log), "nothing cached yet");
  Write(cache / "0000000000000000.RAW", "an older one");
  Check(StoreCachedUnitIndex(cache, key, index, &error), "stored");
  Check(FindCachedUnitIndex(cache, key, &log) == cache / UnitIndexFileName(key), "found");
  {
    std::vector<std::string> names;
    for (const auto& entry : fs::directory_iterator(cache)) names.push_back(entry.path().filename().string());
    Check(names == std::vector<std::string>{torchlight::save_import::Upper(key) + ".RAW"},
          "the file on disk spelled in upper case (listed, so a case-insensitive file system cannot hide it)");
  }
  Check(!fs::exists(cache / "0000000000000000.RAW") && !fs::exists(cache / (UnitIndexFileName(key) + ".tmp")),
        "other cached indexes and the temporary file removed");
  // An index some mod unit could not be read into: loadable under its own name, never found under
  // the key (the next start builds again), and gone once a complete one is stored.
  const std::string incomplete = IncompleteUnitIndexName(key);
  Check(StoreCachedUnitIndex(cache, incomplete, index, &error) && fs::exists(cache / UnitIndexFileName(incomplete)),
        "an incomplete index stored under its own name");
  Check(!fs::exists(cache / UnitIndexFileName(key)) && !FindCachedUnitIndex(cache, key, &log),
        "an incomplete index is not found under the key");
  Check(StoreCachedUnitIndex(cache, key, index, &error) && !fs::exists(cache / UnitIndexFileName(incomplete)) &&
            FindCachedUnitIndex(cache, key, &log) == cache / UnitIndexFileName(key),
        "a complete index replaces the incomplete one");
  Write(cache / UnitIndexFileName(key), "corrupt");
  Check(!FindCachedUnitIndex(cache, key, &log) && !log.empty() && !fs::exists(cache / UnitIndexFileName(key)),
        "a corrupt one is discarded, removed and said");

  fs::remove_all(root);
  if (failures) return EXIT_FAILURE;
  std::puts("unit_cache_test: ok");
  return EXIT_SUCCESS;
}
