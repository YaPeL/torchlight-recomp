// Tests for the merged icon atlases (icon_atlas.h) on synthetic DDS files, imagesets, paks and mod
// folders.

#include "mods/icon_atlas.h"

#include <miniz.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>

namespace m = torchlight::mods;
namespace fs = std::filesystem;

namespace {

int failures = 0;
void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

void Put32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
  for (int i = 0; i < 4; ++i) b[at + i] = uint8_t(v >> (8 * i));
}

// A DDS header for `w` x `h`; `fourcc` empty: 32-bit A8R8G8B8.
std::vector<uint8_t> Header(uint32_t w, uint32_t h, const char* fourcc) {
  std::vector<uint8_t> d(128, 0);
  std::memcpy(d.data(), "DDS ", 4);
  Put32(d, 4, 124);
  Put32(d, 12, h);
  Put32(d, 16, w);
  Put32(d, 76, 32);
  if (fourcc) {
    Put32(d, 80, 0x4);
    std::memcpy(&d[84], fourcc, 4);
  } else {
    Put32(d, 80, 0x41);
    Put32(d, 88, 32);
    Put32(d, 92, 0x00FF0000);
    Put32(d, 96, 0x0000FF00);
    Put32(d, 100, 0x000000FF);
    Put32(d, 104, 0xFF000000);
  }
  return d;
}

// A DXT texture of one colour per 4x4 block: `colour` is 5:6:5, every index 0.
std::vector<uint8_t> Solid(uint32_t w, uint32_t h, const char* fourcc, uint16_t colour, uint8_t alpha4 = 15) {
  std::vector<uint8_t> d = Header(w, h, fourcc);
  const bool dxt1 = std::strcmp(fourcc, "DXT1") == 0, dxt5 = std::strcmp(fourcc, "DXT5") == 0;
  for (uint32_t i = 0; i < (w / 4) * (h / 4); ++i) {
    if (!dxt1) {
      uint8_t alpha[8];
      if (dxt5) {
        const uint8_t a = uint8_t(alpha4 * 17);
        alpha[0] = a;
        alpha[1] = a;
        std::memset(alpha + 2, 0, 6);
      } else {
        std::memset(alpha, alpha4 | alpha4 << 4, 8);
      }
      d.insert(d.end(), alpha, alpha + 8);
    }
    const uint8_t block[8] = {uint8_t(colour), uint8_t(colour >> 8), 0, 0, 0, 0, 0, 0};
    d.insert(d.end(), block, block + 8);
  }
  return d;
}

const uint8_t* Pixel(const m::RgbaImage& img, uint32_t x, uint32_t y) { return &img.rgba[(size_t(y) * img.width + x) * 4]; }

std::string Imageset(const std::string& file, const std::vector<std::string>& images) {
  std::string xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Imageset Name= \"icons\" Imagefile=\"" + file +
                    "\" NativeHorzRes=\"1024\" NativeVertRes=\"768\" AutoScaled=\"true\" >\n";
  for (const auto& image : images) xml += "\t<Image " + image + " />\n";
  return xml + "</Imageset>\n";
}

void WriteFile(const fs::path& path, const std::string& bytes) {
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary).write(bytes.data(), std::streamsize(bytes.size()));
}

std::string Str(const std::vector<uint8_t>& b) { return std::string(b.begin(), b.end()); }

}  // namespace

int main() {
  // Decoding: each format's colour and alpha.
  {
    const auto red = m::DecodeDds(Solid(8, 8, "DXT1", 0xF800));
    Check(red && red->width == 8 && red->height == 8, "DXT1: size");
    Check(red && Pixel(*red, 5, 6)[0] == 255 && Pixel(*red, 5, 6)[1] == 0 && Pixel(*red, 5, 6)[3] == 255,
          "DXT1: an opaque red");
    const auto half = m::DecodeDds(Solid(4, 4, "DXT3", 0x07E0, 8));
    Check(half && Pixel(*half, 0, 0)[1] == 255 && Pixel(*half, 0, 0)[3] == 136, "DXT3: green, explicit alpha 8/15");
    const auto five = m::DecodeDds(Solid(4, 4, "DXT5", 0x001F, 5));
    Check(five && Pixel(*five, 3, 3)[2] == 255 && Pixel(*five, 3, 3)[3] == 85, "DXT5: blue, alpha from the endpoints");
    std::vector<uint8_t> argb = Header(2, 1, nullptr);
    const uint8_t px[8] = {1, 2, 3, 4, 5, 6, 7, 8};  // B, G, R, A in memory
    argb.insert(argb.end(), px, px + 8);
    const auto raw = m::DecodeDds(argb);
    Check(raw && Pixel(*raw, 1, 0)[0] == 7 && Pixel(*raw, 1, 0)[2] == 5 && Pixel(*raw, 1, 0)[3] == 8,
          "A8R8G8B8 by its masks");
    Check(!m::DecodeDds(Header(4, 4, "ATI2")), "an unknown format: none");
    std::vector<uint8_t> cut = Solid(8, 8, "DXT1", 0);
    cut.resize(cut.size() - 1);
    Check(!m::DecodeDds(cut), "a short file: none");
    // Encode then decode: the same pixels.
    const auto again = m::DecodeDds(m::EncodeDdsArgb(*half));
    Check(again && again->rgba == half->rgba, "A8R8G8B8 round trip");
  }

  // The merge: the game's images the mod lacks keep their places, the mod's move right.
  const std::string game_xml = Imageset("media/ui/icons.dds", {
      "Name=\"xbox_only\" XPos=\"0\" YPos=\"0\" Width=\"4\" Height=\"4\"",
      "Name=\"both\" XPos=\"4\" YPos=\"0\" Width=\"4\" Height=\"4\"",
  });
  const std::string mod_xml = Imageset("media/ui/icons.dds", {
      "Name=\"both\" XPos=\"0\" YPos=\"0\" Width=\"4\" Height=\"4\"",
      "Name=\"mod_only\" XPos=\"4\" YPos=\"4\" Width=\"4\" Height=\"4\" XOffset=\"1\"",
  });
  const auto game_tex = Solid(8, 8, "DXT3", 0xF800);   // red
  const auto mod_tex = Solid(8, 8, "DXT1", 0x001F);    // blue
  {
    std::string why;
    const auto merged = m::MergeAtlases(game_xml, game_tex, mod_xml, mod_tex, "tlatlas:/X.DDS", &why);
    Check(merged.has_value(), "merged");
    if (merged) {
      Check(merged->game_only == 1 && merged->from_mod == 2, "one game image kept, the mod's two");
      const auto atlas = m::ParseAtlas(merged->xml);
      Check(atlas && atlas->Attribute(atlas->imageset, "Imagefile") == "tlatlas:/X.DDS" &&
                atlas->Attribute(atlas->imageset, "Name") == "icons" &&
                atlas->Attribute(atlas->imageset, "NativeHorzRes") == "1024",
            "the imageset keeps its name and resolution, names the new texture");
      std::map<std::string, std::pair<std::string, std::string>> at;
      std::string offset;
      for (const auto& image : atlas->images) {
        at[atlas->Attribute(image, "Name")] = {atlas->Attribute(image, "XPos"), atlas->Attribute(image, "YPos")};
        if (atlas->Attribute(image, "Name") == "mod_only") offset = atlas->Attribute(image, "XOffset");
      }
      Check(at.size() == 3, "three images");
      Check(at["xbox_only"] == std::pair<std::string, std::string>{"0", "0"}, "the game's image at its place");
      Check(at["both"] == std::pair<std::string, std::string>{"8", "0"}, "an image in both: the mod's, moved right");
      Check(at["mod_only"] == std::pair<std::string, std::string>{"12", "4"} && offset == "1",
            "the mod's image moved right, its other attributes kept");
      const auto tex = m::DecodeDds(merged->dds);
      Check(tex && tex->width == 16 && tex->height == 8, "a power-of-two texture of both side by side");
      Check(tex && Pixel(*tex, 1, 1)[0] == 255 && Pixel(*tex, 9, 1)[2] == 255, "the game's at the left, the mod's at the right");
    }
  }
  {
    std::string why;
    const std::string superset = Imageset("media/ui/icons.dds", {
        "Name=\"xbox_only\" XPos=\"0\" YPos=\"0\" Width=\"4\" Height=\"4\"",
        "Name=\"both\" XPos=\"4\" YPos=\"0\" Width=\"4\" Height=\"4\"",
        "Name=\"mod_only\" XPos=\"4\" YPos=\"4\" Width=\"4\" Height=\"4\"",
    });
    Check(!m::MergeAtlases(game_xml, game_tex, superset, mod_tex, "x", &why), "the mod's file a superset: no merge");
    std::string renamed = mod_xml;
    renamed.replace(renamed.find("\"icons\""), 7, "\"other\"");
    Check(!m::MergeAtlases(game_xml, game_tex, renamed, mod_tex, "x", &why) && why.find("Name") != std::string::npos,
          "another imageset name: no merge");
    const std::string outside = Imageset("media/ui/icons.dds", {"Name=\"big\" XPos=\"4\" YPos=\"0\" Width=\"8\" Height=\"4\""});
    Check(!m::MergeAtlases(game_xml, game_tex, outside, mod_tex, "x", &why), "an image outside its texture: no merge");
    Check(!m::MergeAtlases(game_xml, game_tex, mod_xml, std::vector<uint8_t>(10, 0), "x", &why),
          "a texture that does not decode: no merge");
    // Two 8192-wide atlases would need a 16384-wide one, past the Xbox's largest side.
    std::vector<uint8_t> wide = Header(8192, 8, nullptr);
    wide.resize(wide.size() + 8192 * 8 * 4, 0);
    Check(!m::MergeAtlases(game_xml, wide, mod_xml, wide, "x", &why) && why.starts_with("too large"),
          "a merged atlas wider than 8192: no merge");
  }

  // Files: a synthetic pak and mods folder.
  {
    const fs::path root = fs::temp_directory_path() / "icon_atlas_test";
    fs::remove_all(root);
    mz_zip_archive zip{};
    Check(mz_zip_writer_init_heap(&zip, 0, 0), "zip writer");
    auto add = [&](const char* name, const std::string& bytes) {
      mz_zip_writer_add_mem(&zip, name, bytes.data(), bytes.size(), MZ_DEFAULT_COMPRESSION);
    };
    add("media/UI/itemicons/icons.imageset", game_xml);
    add("media/ui/icons.dds", Str(game_tex));
    add("media/UI/other.imageset", game_xml);
    void* buffer = nullptr;
    size_t size = 0;
    mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size);
    WriteFile(root / "pak.zip", std::string(static_cast<char*>(buffer), size));
    mz_zip_writer_end(&zip);
    // The first mod in the search order replaces icons; a later one too (not taken); a disabled one
    // replaces other.imageset (not taken).
    WriteFile(root / "mods" / "First" / "MEDIA" / "ui" / "ItemIcons" / "ICONS.imageset", mod_xml);
    WriteFile(root / "mods" / "First" / "media" / "UI" / "Icons.DDS", Str(mod_tex));
    WriteFile(root / "mods" / "Later" / "media" / "ui" / "itemicons" / "icons.imageset", game_xml);
    WriteFile(root / "mods" / "Off" / "media" / "ui" / "other.imageset", mod_xml);
    m::ModPlan plan;
    plan.mods = {{"First", 1}, {"Later", 2}, {"Off", -1}};
    std::vector<std::string> log;
    WriteFile(root / "out" / "STALE.DDS", "old");
    const auto written = m::WriteMergedAtlases(root / "pak.zip", root / "mods", plan, root / "out", "tlatlas:", log);
    Check(written.size() == 1 && written[0].key == "MEDIA/UI/ITEMICONS/ICONS.IMAGESET" && written[0].mod_folder == "First",
          "the first mod's file merged, by the game's path in upper case");
    Check(!fs::exists(root / "out" / "STALE.DDS"), "the folder emptied first");
    std::ifstream in(root / "out" / "MEDIA/UI/ITEMICONS/ICONS.IMAGESET", std::ios::binary);
    const std::string xml((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto atlas = m::ParseAtlas(xml);
    const std::string file = atlas ? atlas->Attribute(atlas->imageset, "Imagefile") : "";
    Check(file.starts_with("tlatlas:/MEDIA/UI/ITEMICONS/ICONS.") && file.ends_with(".DDS"),
          "the texture named on our device, by the inputs' hash");
    Check(fs::exists(root / "out" / file.substr(std::string("tlatlas:/").size())), "the texture written");
    // The same inputs: the same name; another mod texture: another name.
    log.clear();
    m::WriteMergedAtlases(root / "pak.zip", root / "mods", plan, root / "out", "tlatlas:", log);
    std::ifstream again(root / "out" / "MEDIA/UI/ITEMICONS/ICONS.IMAGESET", std::ios::binary);
    const std::string xml2((std::istreambuf_iterator<char>(again)), std::istreambuf_iterator<char>());
    Check(xml2 == xml, "deterministic");
    WriteFile(root / "mods" / "First" / "media" / "UI" / "Icons.DDS", Str(Solid(8, 8, "DXT1", 0x07E0)));
    m::WriteMergedAtlases(root / "pak.zip", root / "mods", plan, root / "out", "tlatlas:", log);
    std::ifstream third(root / "out" / "MEDIA/UI/ITEMICONS/ICONS.IMAGESET", std::ios::binary);
    const std::string xml3((std::istreambuf_iterator<char>(third)), std::istreambuf_iterator<char>());
    Check(xml3 != xml, "a changed mod texture: another texture name");
    // No mods: nothing written.
    log.clear();
    Check(m::WriteMergedAtlases(root / "pak.zip", root / "mods", {}, root / "out", "tlatlas:", log).empty(),
          "no mods: nothing");
    fs::remove_all(root);
  }

  if (failures) {
    std::fprintf(stderr, "%d failure(s)\n", failures);
    return EXIT_FAILURE;
  }
  std::printf("icon_atlas_test: ok\n");
  return EXIT_SUCCESS;
}
