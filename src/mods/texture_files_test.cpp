// Which file a named texture comes from with mods and the replacements folder (texture_files.h).

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "mods/texture_files.h"

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
void Touch(const fs::path& p) {
  fs::create_directories(p.parent_path());
  std::ofstream(p) << "x";
}
}  // namespace

int main() {
  const fs::path root = fs::temp_directory_path() /
                        ("tl_texture_files_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  // Two mods and the replacements folder; the pak holds the game's two textures.
  Touch(root / "a" / "media" / "levelsets" / "town1" / "Bricks.dds");    // a: a game texture's path
  Touch(root / "a" / "media" / "models" / "sword" / "new_sword.dds");    // a: its own new texture
  Touch(root / "b" / "Media" / "Models" / "Sword" / "NEW_SWORD.DDS");    // b: the same new path
  Touch(root / "b" / "media" / "models" / "axe" / "only_b.dds");
  Touch(root / "r" / "media" / "levelsets" / "town1" / "grass001.dds");  // replaces a pak texture
  TextureLocations locations;
  locations.replacements = root / "r";
  locations.mods = {{"MOD A", root / "a"}, {"MOD B", root / "b"}};
  const TextureResolver resolver(locations);
  auto pak = [](std::string_view key) {
    return key == "MEDIA/LEVELSETS/TOWN1/BRICKS.DDS" || key == "MEDIA/LEVELSETS/TOWN1/GRASS001.DDS";
  };

  Check(TextureNameKey("\\media\\Levelsets/town1/x.dds") == "MEDIA/LEVELSETS/TOWN1/X.DDS", "the key");

  // A game group: replacements, then the pak, then the mods.
  auto f = resolver.Find("MEDIA/LEVELSETS/TOWN1/BRICKS.DDS", "0ZIP0", pak);
  Check(f.source == TextureSource::kPak, "a game material keeps the pak's file over a mod's same path");
  f = resolver.Find("MEDIA/LEVELSETS/TOWN1/GRASS001.DDS", "ZIP", pak);
  Check(f.source == TextureSource::kReplacements && f.path.filename() == "grass001.dds",
        "the replacements folder comes before the pak");
  f = resolver.Find("MEDIA/MODELS/AXE/ONLY_B.DDS", "General", pak);
  Check(f.source == TextureSource::kMod && f.path.filename() == "only_b.dds",
        "not in the game's groups: the mods in activation order");

  // A mod's group: that mod's folder first, ignoring case.
  f = resolver.Find("MEDIA/MODELS/SWORD/NEW_SWORD.DDS", "MOD B", pak);
  Check(f.source == TextureSource::kMod && f.path.filename() == "NEW_SWORD.DDS", "the mod's own folder first");
  f = resolver.Find("MEDIA/MODELS/SWORD/NEW_SWORD.DDS", "MOD A", pak);
  Check(f.source == TextureSource::kMod && f.path.filename() == "new_sword.dds", "each mod its own file");
  f = resolver.Find("MEDIA/LEVELSETS/TOWN1/GRASS001.DDS", "MOD B", pak);
  Check(f.source == TextureSource::kReplacements, "not in the mod: the game's groups next");
  f = resolver.Find("MEDIA/MODELS/AXE/ONLY_B.DDS", "MOD A", pak);
  Check(f.source == TextureSource::kMod && f.path.filename() == "only_b.dds", "then the other mods");

  // Nowhere.
  f = resolver.Find("MEDIA/NOWHERE.DDS", "MOD A", pak);
  Check(f.source == TextureSource::kNone, "not found anywhere");

  // The session's resolver and pak names.
  SetTextureLocations({});
  Check(CurrentTextureResolver() == nullptr, "no mods and no replacements: no resolver");
  SetTextureLocations(locations);
  Check(CurrentTextureResolver() != nullptr, "a resolver with mods");
  SetPakNames({"media/levelsets/town1/bricks.dds"});
  Check(PakHas("MEDIA\\LEVELSETS\\TOWN1\\BRICKS.DDS") && !PakHas("MEDIA/X.DDS"), "the pak's names");

  std::error_code ec;
  fs::remove_all(root, ec);
  if (failures) return EXIT_FAILURE;
  std::puts("texture_files_test: ok");
  return EXIT_SUCCESS;
}
