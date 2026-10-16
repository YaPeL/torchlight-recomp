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
  // Files a mod's data names through the data loader, which gives them back by their full path
  // with the mod's device (docs/mods.md 7j): a unit's TEXTURE_OVERRIDE and [TEXTURE_REPLACE]
  // blocks, a wardrobe piece (as the .dat names it, .png), a shared texture a mod ships.
  Touch(root / "b" / "media" / "units" / "items" / "Scroll_Blue.dds");
  Touch(root / "b" / "media" / "units" / "monsters" / "spider_red.dds");
  Touch(root / "b" / "Media" / "Wardrobe" / "Destroyer" / "starting_boots.png");
  Touch(root / "b" / "media" / "sharedtextures" / "rimlight.dds");
  TextureLocations locations;
  locations.replacements = root / "r";
  locations.mods = {{"MOD A", root / "a", "tlmod000:"}, {"MOD B", root / "b", "tlmod001:"}};
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

  // An absolute name, as the data loader gives a mod's file: only that mod's folder.
  f = resolver.Find("TLMOD001:/MEDIA/MODELS/AXE/ONLY_B.DDS", "General", pak);
  Check(f.source == TextureSource::kMod && f.path.filename() == "only_b.dds", "a mod's device: its folder");
  f = resolver.Find("tlmod000:\\media\\models\\axe\\only_b.dds", "General", pak);
  Check(f.source == TextureSource::kNone, "another mod's device: not looked up elsewhere");
  f = resolver.Find("TLREPL:/MEDIA/LEVELSETS/TOWN1/GRASS001.DDS", "General", pak);
  Check(f.source == TextureSource::kReplacements, "the replacements' device: that folder");

  // The data loader's paths, one case each (the names as the guest asks for them).
  f = resolver.Find("TLMOD001:/MEDIA/UNITS/ITEMS/SCROLL_BLUE.DDS", "General", pak);
  Check(f.source == TextureSource::kMod && f.path.filename() == "Scroll_Blue.dds", "a unit's TEXTURE_OVERRIDE");
  f = resolver.Find("TLMOD001:/MEDIA/UNITS/MONSTERS/SPIDER_RED.DDS", "Autodetect", pak);
  Check(f.source == TextureSource::kMod && f.path.filename() == "spider_red.dds", "a [TEXTURE_REPLACE] block");
  f = resolver.Find("TLMOD001:/MEDIA/WARDROBE/DESTROYER/STARTING_BOOTS.PNG", "General", pak);
  Check(f.source == TextureSource::kMod && f.path.filename() == "starting_boots.png", "a wardrobe piece, as named");
  f = resolver.Find("TLMOD001:/MEDIA/WARDROBE/DESTROYER/STARTING_BOOTS.DDS", "General", pak);
  Check(f.source == TextureSource::kNone, "the name as the guest asks for it, no other extension");
  f = resolver.Find("TLMOD001:/MEDIA/SHAREDTEXTURES/RIMLIGHT.DDS", "General", pak);
  Check(f.source == TextureSource::kMod && f.path.filename() == "rimlight.dds", "a shared texture a mod ships");
  f = resolver.Find("GAME:\\MEDIA\\SHAREDTEXTURES\\RIMLIGHT.DDS", "General", pak);
  Check(f.source == TextureSource::kNone, "a device that is no mod's: nothing");

  // A generated file (a merged icon atlas, mods/icon_atlas.h): by its device only.
  {
    Touch(root / "g" / "MEDIA" / "UI" / "ITEMICONS" / "ITEMICONS16.0123.DDS");
    TextureLocations with = locations;
    with.generated = root / "g";
    with.generated_device = "tlatlas:";
    const TextureResolver gen(with);
    TextureFile g = gen.Find("TLATLAS:/MEDIA/UI/ITEMICONS/ITEMICONS16.0123.DDS", "Autodetect", pak);
    Check(g.source == TextureSource::kGenerated && g.path.filename() == "ITEMICONS16.0123.DDS",
          "a generated atlas by its device");
    g = gen.Find("tlatlas:\\media\\ui\\itemicons\\itemicons16.0123.dds", "MOD A", pak);
    Check(g.source == TextureSource::kGenerated, "any case and separator, any group");
    g = gen.Find("MEDIA/LEVELSETS/TOWN1/BRICKS.DDS", "General", pak);
    Check(g.source == TextureSource::kPak, "the pak's name still the pak's");
    g = gen.Find("TLATLAS:/MEDIA/UI/ITEMICONS/OTHER.DDS", "General", pak);
    Check(g.source == TextureSource::kNone, "not in the generated folder: nothing");
    Check(FromHostFile(TextureSource::kGenerated) && FromHostFile(TextureSource::kMod) &&
              !FromHostFile(TextureSource::kPak),
          "generated files are kept like a mod's");
    TextureLocations only;
    only.generated = root / "g";
    only.generated_device = "tlatlas:";
    Check(!TextureResolver(only).empty(), "a resolver with generated files only");
  }

  // The pad prompts' textures (mods/pad_prompts.h): after the replacements, before the pak.
  {
    Touch(root / "p" / "media" / "levelsets" / "town1" / "bricks.dds");
    Touch(root / "p" / "media" / "levelsets" / "town1" / "grass001.dds");
    Touch(root / "p" / "media" / "ui" / "widgets.dds");
    TextureLocations with = locations;
    with.prompts = root / "p";
    const TextureResolver prompts(with);
    TextureFile p = prompts.Find("MEDIA/LEVELSETS/TOWN1/BRICKS.DDS", "General", pak);
    Check(p.source == TextureSource::kGenerated && p.path.parent_path().filename() == "town1" &&
              p.path.string().find("/p/") != std::string::npos,
          "a prompt texture before the pak's");
    p = prompts.Find("MEDIA/LEVELSETS/TOWN1/GRASS001.DDS", "General", pak);
    Check(p.source == TextureSource::kReplacements, "the player's replacements before the prompts");
    p = prompts.Find("tlprompt:/media/levelsets/town1/bricks.dds", "General", pak);
    Check(p.source == TextureSource::kGenerated, "by its device too");
    p = prompts.Find("MEDIA/UI/WIDGETS.DDS", "MOD A", pak);
    Check(p.source == TextureSource::kGenerated, "from a mod's group without the file: the game's groups");
    p = prompts.Find("MEDIA/LEVELSETS/TOWN1/BRICKS.DDS", "MOD A", pak);
    Check(p.source == TextureSource::kMod, "a mod's own file still first in its group");
    TextureLocations only;
    only.prompts = root / "p";
    Check(!TextureResolver(only).empty(), "a resolver with prompts only");
  }

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
