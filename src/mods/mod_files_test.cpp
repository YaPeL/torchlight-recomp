// Finding a mod's file whatever the letter case and separators (mod_files.h), on synthetic mods.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "mods/mod_files.h"

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
void Write(const fs::path& path) {
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << "x";
}
}  // namespace

int main() {
  Check(NormalizeModPath(u"media\\Units/items\\x.dat") == u"MEDIA/UNITS/ITEMS/X.DAT", "case and separators");
  Check(NormalizeModPath(u"./media/x.dat") == u"MEDIA/X.DAT" && NormalizeModPath(u"/media/x.dat") == u"MEDIA/X.DAT",
        "leading ./ and /");

  // Table logic: the first mod in search order that has the file gives its spelling.
  {
    ModFileTable t;
    t.Add(2, u"MEDIA/UNITS/ITEMS/AXE.DAT");
    t.Add(0, u"media/units/items/axe.dat");
    t.Add(1, u"Media/Units/Items/Bow.dat");
    Check(t.Spelling(u"MEDIA\\UNITS\\ITEMS\\AXE.DAT") == u"media/units/items/axe.dat",
          "the first mod in search order wins");
    Check(t.Spelling(u"media/units/items/bow.DAT") == u"Media/Units/Items/Bow.dat", "spelled as on disk");
    Check(!t.Spelling(u"MEDIA/UNITS/ITEMS/NONE.DAT"), "a file no mod has: none");
  }

  // Scanning folders: enabled mods only, paths relative to the mod, '/' between folders.
  const fs::path root = fs::temp_directory_path() /
      ("tl_mod_files_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  Write(root / "a" / "media" / "units" / "items" / "tl_test" / "TL_TEST_SWORD.DAT");
  Write(root / "b" / "MEDIA" / "UNITS" / "ITEMS" / "TL_TEST" / "tl_test_sword.dat");
  Write(root / "c" / "media" / "x.dat");
  ModPlan plan;
  plan.mods = {{"a", 0}, {"b", 1}, {"c", -1}};
  const ModFileTable table = ModFileTable::Scan(root, plan);
  Check(table.size() == 1, "one normalized path (the disabled mod's file left out)");
  Check(table.Spelling(u"MEDIA/UNITS/ITEMS/TL_TEST/TL_TEST_SWORD.DAT") == u"media/units/items/tl_test/TL_TEST_SWORD.DAT",
        "the unit index's spelling finds the first mod's file");
  Check(!table.Spelling(u"MEDIA/X.DAT"), "a disabled mod's file is not found");
  fs::remove_all(root);

  if (failures) return EXIT_FAILURE;
  std::puts("mod_files_test: ok");
  return EXIT_SUCCESS;
}
