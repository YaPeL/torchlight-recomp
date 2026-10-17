#include "mods/icon_atlas.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <system_error>

#include "mods/class_list.h"
#include "save_import/pak.h"

namespace torchlight::mods {

namespace {

using save_import::Upper;

uint32_t U32(const std::vector<uint8_t>& b, size_t at) {
  return uint32_t(b[at]) | uint32_t(b[at + 1]) << 8 | uint32_t(b[at + 2]) << 16 | uint32_t(b[at + 3]) << 24;
}

void Put32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
  for (int i = 0; i < 4; ++i) b[at + i] = uint8_t(v >> (8 * i));
}

constexpr size_t kHeader = 128;  // "DDS " and the 124-byte header (no DX10 extension)
constexpr uint32_t kFourCcFlag = 0x4;
constexpr uint32_t kMaxSide = 8192;  // the largest texture side the Xbox takes

uint32_t FourCc(const char (&s)[5]) { return uint32_t(s[0]) | uint32_t(s[1]) << 8 | uint32_t(s[2]) << 16 | uint32_t(s[3]) << 24; }

// A 5:6:5 colour as RGB.
void Rgb565(uint16_t c, uint8_t out[3]) {
  out[0] = uint8_t((c >> 11 & 31) * 255 / 31);
  out[1] = uint8_t((c >> 5 & 63) * 255 / 63);
  out[2] = uint8_t((c & 31) * 255 / 31);
}

// One 4x4 colour block (8 bytes) into `px` (16 RGBA pixels). `four_colour` forces the four-colour
// mode (DXT3 and DXT5); DXT1 picks it by the endpoints' order, and its three-colour mode has a
// transparent fourth entry.
void ColourBlock(const uint8_t* b, bool four_colour, uint8_t px[16][4]) {
  const uint16_t c0 = uint16_t(b[0] | b[1] << 8), c1 = uint16_t(b[2] | b[3] << 8);
  uint8_t pal[4][4] = {};
  Rgb565(c0, pal[0]);
  Rgb565(c1, pal[1]);
  pal[0][3] = pal[1][3] = 255;
  if (four_colour || c0 > c1) {
    for (int k = 0; k < 3; ++k) {
      pal[2][k] = uint8_t((2 * pal[0][k] + pal[1][k]) / 3);
      pal[3][k] = uint8_t((pal[0][k] + 2 * pal[1][k]) / 3);
    }
    pal[2][3] = pal[3][3] = 255;
  } else {
    for (int k = 0; k < 3; ++k) pal[2][k] = uint8_t((pal[0][k] + pal[1][k]) / 2);
    pal[2][3] = 255;
    // pal[3] stays transparent black.
  }
  const uint32_t idx = uint32_t(b[4]) | uint32_t(b[5]) << 8 | uint32_t(b[6]) << 16 | uint32_t(b[7]) << 24;
  for (int i = 0; i < 16; ++i) std::copy_n(pal[idx >> (2 * i) & 3], 4, px[i]);
}

// A channel by its mask, scaled to 8 bits.
uint8_t Channel(uint32_t v, uint32_t mask, uint8_t none) {
  if (!mask) return none;
  int shift = 0;
  while (!(mask >> shift & 1)) ++shift;
  const uint32_t max = mask >> shift;
  return uint8_t(((v & mask) >> shift) * 255 / max);
}

std::string Quote(std::string_view s) {
  std::string out;
  for (char c : s) {
    if (c == '&') out += "&amp;";
    else if (c == '"') out += "&quot;";
    else if (c == '<') out += "&lt;";
    else out += c;
  }
  return out;
}

// The attributes of one element's start tag, `tag` being the text after its name up to '>'.
AtlasXml::Attributes Attributes(std::string_view tag) {
  AtlasXml::Attributes out;
  size_t i = 0;
  auto space = [&] { while (i < tag.size() && (tag[i] == ' ' || tag[i] == '\t' || tag[i] == '\r' || tag[i] == '\n')) ++i; };
  while (true) {
    space();
    const size_t name_start = i;
    while (i < tag.size() && tag[i] != '=' && tag[i] != ' ' && tag[i] != '\t' && tag[i] != '\r' && tag[i] != '\n' &&
           tag[i] != '/' && tag[i] != '>') {
      ++i;
    }
    if (i == name_start) break;
    const std::string name(tag.substr(name_start, i - name_start));
    space();
    if (i >= tag.size() || tag[i] != '=') break;
    ++i;
    space();
    if (i >= tag.size() || (tag[i] != '"' && tag[i] != '\'')) break;
    const char q = tag[i++];
    const size_t end = tag.find(q, i);
    if (end == std::string_view::npos) break;
    out.emplace_back(name, std::string(tag.substr(i, end - i)));
    i = end + 1;
  }
  return out;
}

// The start tags named `element` in `xml`, as attribute lists.
std::vector<AtlasXml::Attributes> Elements(std::string_view xml, std::string_view element) {
  std::vector<AtlasXml::Attributes> out;
  const std::string open = "<" + std::string(element);
  for (size_t pos = 0; (pos = xml.find(open, pos)) != std::string_view::npos; pos += open.size()) {
    const size_t after = pos + open.size();
    if (after >= xml.size() || !(xml[after] == ' ' || xml[after] == '\t' || xml[after] == '\r' || xml[after] == '\n')) {
      continue;  // "<ImageSomething", not "<Image "
    }
    const size_t end = xml.find('>', after);
    if (end == std::string_view::npos) break;
    out.push_back(Attributes(xml.substr(after, end - after)));
  }
  return out;
}

std::optional<long> Number(const std::string& s) {
  if (s.empty()) return std::nullopt;
  size_t used = 0;
  try {
    const long v = std::stol(s, &used);
    return used == s.size() ? std::optional<long>(v) : std::nullopt;
  } catch (...) {
    return std::nullopt;
  }
}

void Set(AtlasXml::Attributes& a, std::string_view name, const std::string& value) {
  for (auto& [n, v] : a) {
    if (n == name) {
      v = value;
      return;
    }
  }
  a.emplace_back(std::string(name), value);
}

uint32_t PowerOfTwo(uint32_t v) {
  uint32_t p = 1;
  while (p < v) p <<= 1;
  return p;
}

// FNV-1a over the inputs: the merged texture's name changes with them.
uint64_t Fnv(uint64_t h, std::string_view bytes) {
  for (unsigned char c : bytes) h = (h ^ c) * 0x100000001B3ull;
  return h;
}

std::vector<uint8_t> ReadFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// The file at `relative` ('/' or '\' separated) under `root`, each part matched in any case. Folders
// whose names differ only by case are all searched, as the game's file map lists every one.
std::optional<std::filesystem::path> FindNoCase(const std::filesystem::path& root, std::string_view relative) {
  std::vector<std::string> parts(1);
  for (char c : relative) {
    if (c == '/' || c == '\\') {
      if (!parts.back().empty()) parts.emplace_back();
    } else {
      parts.back() += c;
    }
  }
  if (parts.back().empty()) parts.pop_back();
  std::vector<std::filesystem::path> at = {root};
  for (size_t i = 0; i < parts.size(); ++i) {
    std::vector<std::filesystem::path> next;
    for (const auto& dir : at) {
      std::error_code ec;
      for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (Upper(entry.path().filename().string()) == Upper(parts[i])) next.push_back(entry.path());
      }
    }
    std::sort(next.begin(), next.end());
    at = std::move(next);
  }
  for (const auto& path : at) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(path, ec)) return path;
  }
  return std::nullopt;
}

std::string PakKey(std::string_view path) {
  std::string key;
  for (char c : path) key += c == '\\' ? '/' : char(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
  while (!key.empty() && key.front() == '/') key.erase(key.begin());
  return key;
}

}  // namespace

std::string AtlasXml::Attribute(const Attributes& a, std::string_view name) const {
  for (const auto& [n, v] : a) {
    if (n == name) return v;
  }
  return {};
}

std::optional<RgbaImage> DecodeDds(const std::vector<uint8_t>& dds) {
  if (dds.size() < kHeader || U32(dds, 0) != FourCc("DDS ") || U32(dds, 4) != 124) return std::nullopt;
  RgbaImage img;
  img.height = U32(dds, 12);
  img.width = U32(dds, 16);
  if (!img.width || !img.height || img.width > 8192 || img.height > 8192) return std::nullopt;
  img.rgba.assign(size_t(img.width) * img.height * 4, 0);
  const uint32_t pf_flags = U32(dds, 80), fourcc = U32(dds, 84);
  const uint8_t* data = dds.data() + kHeader;
  const size_t available = dds.size() - kHeader;
  auto put = [&](uint32_t x, uint32_t y, const uint8_t p[4]) {
    if (x < img.width && y < img.height) std::copy_n(p, 4, &img.rgba[(size_t(y) * img.width + x) * 4]);
  };
  if (pf_flags & kFourCcFlag) {
    const bool dxt1 = fourcc == FourCc("DXT1"), dxt3 = fourcc == FourCc("DXT3"), dxt5 = fourcc == FourCc("DXT5");
    if (!dxt1 && !dxt3 && !dxt5) return std::nullopt;
    const size_t block = dxt1 ? 8 : 16;
    const uint32_t bw = (img.width + 3) / 4, bh = (img.height + 3) / 4;
    if (available < size_t(bw) * bh * block) return std::nullopt;
    for (uint32_t by = 0; by < bh; ++by) {
      for (uint32_t bx = 0; bx < bw; ++bx) {
        const uint8_t* b = data + (size_t(by) * bw + bx) * block;
        uint8_t px[16][4];
        ColourBlock(dxt1 ? b : b + 8, !dxt1, px);
        if (dxt3) {
          for (int i = 0; i < 16; ++i) px[i][3] = uint8_t((b[i / 2] >> (4 * (i & 1)) & 15) * 17);
        } else if (dxt5) {
          uint8_t a[8] = {b[0], b[1]};
          if (a[0] > a[1]) {
            for (int k = 1; k < 7; ++k) a[k + 1] = uint8_t(((7 - k) * a[0] + k * a[1]) / 7);
          } else {
            for (int k = 1; k < 5; ++k) a[k + 1] = uint8_t(((5 - k) * a[0] + k * a[1]) / 5);
            a[6] = 0;
            a[7] = 255;
          }
          uint64_t bits = 0;
          for (int k = 0; k < 6; ++k) bits |= uint64_t(b[2 + k]) << (8 * k);
          for (int i = 0; i < 16; ++i) px[i][3] = a[bits >> (3 * i) & 7];
        }
        for (int i = 0; i < 16; ++i) put(bx * 4 + i % 4, by * 4 + i / 4, px[i]);
      }
    }
    return img;
  }
  const uint32_t bits = U32(dds, 88);
  if (bits != 32 && bits != 24) return std::nullopt;
  const uint32_t rm = U32(dds, 92), gm = U32(dds, 96), bm = U32(dds, 100);
  const uint32_t am = (pf_flags & 0x1) ? U32(dds, 104) : 0;  // DDPF_ALPHAPIXELS
  const size_t bpp = bits / 8;
  if (available < size_t(img.width) * img.height * bpp) return std::nullopt;
  for (uint32_t y = 0; y < img.height; ++y) {
    for (uint32_t x = 0; x < img.width; ++x) {
      const uint8_t* p = data + (size_t(y) * img.width + x) * bpp;
      const uint32_t v = uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | (bpp == 4 ? uint32_t(p[3]) << 24 : 0);
      const uint8_t out[4] = {Channel(v, rm, 0), Channel(v, gm, 0), Channel(v, bm, 0), Channel(v, am, 255)};
      put(x, y, out);
    }
  }
  return img;
}

std::vector<uint8_t> EncodeDdsArgb(const RgbaImage& image) {
  std::vector<uint8_t> out(kHeader + size_t(image.width) * image.height * 4, 0);
  Put32(out, 0, FourCc("DDS "));
  Put32(out, 4, 124);
  Put32(out, 8, 0x100F);  // caps, height, width, pitch, pixel format
  Put32(out, 20, image.width * 4);
  Put32(out, 12, image.height);
  Put32(out, 16, image.width);
  Put32(out, 76, 32);
  Put32(out, 80, 0x41);  // RGB with alpha
  Put32(out, 88, 32);
  Put32(out, 92, 0x00FF0000);
  Put32(out, 96, 0x0000FF00);
  Put32(out, 100, 0x000000FF);
  Put32(out, 104, 0xFF000000);
  Put32(out, 108, 0x1000);  // a texture
  for (size_t i = 0; i < size_t(image.width) * image.height; ++i) {
    const uint8_t* p = &image.rgba[i * 4];
    uint8_t* q = &out[kHeader + i * 4];
    q[0] = p[2];
    q[1] = p[1];
    q[2] = p[0];
    q[3] = p[3];
  }
  return out;
}

std::optional<AtlasXml> ParseAtlas(std::string_view xml) {
  auto sets = Elements(xml, "Imageset");
  if (sets.size() != 1) return std::nullopt;
  AtlasXml atlas;
  atlas.imageset = std::move(sets.front());
  if (atlas.Attribute(atlas.imageset, "Name").empty() || atlas.Attribute(atlas.imageset, "Imagefile").empty()) {
    return std::nullopt;
  }
  atlas.images = Elements(xml, "Image");
  return atlas;
}

std::optional<MergedAtlas> MergeAtlases(std::string_view game_xml, const std::vector<uint8_t>& game_texture,
                                        std::string_view mod_xml, const std::vector<uint8_t>& mod_texture,
                                        const std::string& texture_name, std::string* why) {
  auto fail = [&](std::string reason) -> std::optional<MergedAtlas> {
    if (why) *why = std::move(reason);
    return std::nullopt;
  };
  const auto game = ParseAtlas(game_xml);
  const auto mod = ParseAtlas(mod_xml);
  if (!game || !mod) return fail("an imageset that does not parse");
  for (const char* attr : {"Name", "NativeHorzRes", "NativeVertRes", "AutoScaled"}) {
    if (game->Attribute(game->imageset, attr) != mod->Attribute(mod->imageset, attr)) {
      return fail(std::string("another ") + attr + " (\"" + mod->Attribute(mod->imageset, attr) + "\")");
    }
  }
  std::set<std::string> mod_names;
  for (const auto& image : mod->images) mod_names.insert(mod->Attribute(image, "Name"));
  size_t game_only = 0;
  for (const auto& image : game->images) game_only += !mod_names.contains(game->Attribute(image, "Name"));
  if (!game_only) return fail("the mod's file has every image of the game's");

  const auto gi = DecodeDds(game_texture);
  const auto mi = DecodeDds(mod_texture);
  if (!gi) return fail("the game's texture does not decode");
  if (!mi) return fail("the mod's texture does not decode");
  auto inside = [](const AtlasXml& atlas, const AtlasXml::Attributes& image, const RgbaImage& tex) {
    const auto x = Number(atlas.Attribute(image, "XPos")), y = Number(atlas.Attribute(image, "YPos"));
    const auto w = Number(atlas.Attribute(image, "Width")), h = Number(atlas.Attribute(image, "Height"));
    return x && y && w && h && *x >= 0 && *y >= 0 && *w >= 0 && *h >= 0 && *x + *w <= long(tex.width) &&
           *y + *h <= long(tex.height);
  };
  for (const auto& image : game->images) {
    if (!inside(*game, image, *gi)) return fail("a game image outside its texture: " + game->Attribute(image, "Name"));
  }
  for (const auto& image : mod->images) {
    if (!inside(*mod, image, *mi)) return fail("a mod image outside its texture: " + mod->Attribute(image, "Name"));
  }

  // The game's texture at the left, the mod's at its right, in a power-of-two texture.
  RgbaImage out;
  out.width = PowerOfTwo(gi->width + mi->width);
  out.height = PowerOfTwo(std::max(gi->height, mi->height));
  if (out.width > kMaxSide || out.height > kMaxSide) {
    return fail("too large (" + std::to_string(out.width) + "x" + std::to_string(out.height) + ")");
  }
  out.rgba.assign(size_t(out.width) * out.height * 4, 0);
  auto blit = [&](const RgbaImage& src, uint32_t x0) {
    for (uint32_t y = 0; y < src.height; ++y) {
      std::copy_n(&src.rgba[size_t(y) * src.width * 4], size_t(src.width) * 4, &out.rgba[(size_t(y) * out.width + x0) * 4]);
    }
  };
  blit(*gi, 0);
  blit(*mi, gi->width);

  AtlasXml::Attributes head = game->imageset;
  Set(head, "Imagefile", texture_name);
  std::string xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Imageset";
  for (const auto& [n, v] : head) xml += " " + n + "=\"" + Quote(v) + "\"";
  xml += " >\n";
  auto line = [&](const AtlasXml::Attributes& image) {
    xml += "\t<Image";
    for (const auto& [n, v] : image) xml += " " + n + "=\"" + Quote(v) + "\"";
    xml += " />\n";
  };
  MergedAtlas merged;
  for (const auto& image : game->images) {
    if (mod_names.contains(game->Attribute(image, "Name"))) continue;
    line(image);
    ++merged.game_only;
  }
  for (auto image : mod->images) {
    Set(image, "XPos", std::to_string(*Number(mod->Attribute(image, "XPos")) + long(gi->width)));
    line(image);
    ++merged.from_mod;
  }
  xml += "</Imageset>\n";
  merged.xml = std::move(xml);
  merged.dds = EncodeDdsArgb(out);
  return merged;
}

std::vector<WrittenAtlas> WriteMergedAtlases(const std::filesystem::path& pak_path, const std::filesystem::path& mods_folder,
                                             const ModPlan& plan, const std::filesystem::path& out,
                                             const std::string& device, std::vector<std::string>& log) {
  std::vector<WrittenAtlas> written;
  std::error_code ec;
  std::filesystem::remove_all(out, ec);
  std::filesystem::create_directories(out, ec);
  std::string error;
  const auto pak = save_import::Pak::Open(pak_path, error);
  if (!pak) {
    log.push_back("the pak does not open: " + error);
    return written;
  }
  std::map<std::string, uint32_t> entries;  // lower-case path -> index
  pak->ForEach([&](uint32_t index, const std::string& name) {
    entries.emplace(name, index);
    return true;
  });
  std::vector<const PlannedMod*> enabled;
  for (const PlannedMod& mod : plan.mods) {
    if (mod.priority >= 0) enabled.push_back(&mod);
  }
  for (const auto& [name, index] : entries) {
    if (!name.starts_with("media/ui/") || !name.ends_with(".imageset")) continue;
    const PlannedMod* owner = nullptr;
    std::optional<std::filesystem::path> mod_file;
    for (const PlannedMod* mod : enabled) {
      if ((mod_file = FindNoCase(mods_folder / mod->folder, name))) {
        owner = mod;
        break;
      }
    }
    if (!owner) continue;
    std::vector<uint8_t> game_bytes;
    if (!pak->Read(index, game_bytes, error)) {
      log.push_back(name + ": the pak's file does not read");
      continue;
    }
    const std::string game_xml = LayoutText(game_bytes);
    const std::string mod_xml = LayoutText(ReadFile(*mod_file));
    const auto game = ParseAtlas(game_xml);
    const auto mod = ParseAtlas(mod_xml);
    if (!game || !mod) {
      log.push_back(name + " (" + owner->folder + "): an imageset that does not parse; the mod's is used");
      continue;
    }
    std::vector<uint8_t> game_texture, mod_texture;
    const auto game_tex = entries.find(PakKey(game->Attribute(game->imageset, "Imagefile")));
    if (game_tex == entries.end() || !pak->Read(game_tex->second, game_texture, error)) {
      log.push_back(name + ": the pak's texture is missing; the mod's file is used");
      continue;
    }
    // The mod's Imagefile as OGRE finds it from the mod's group: the mod's folder, then the pak.
    const std::string mod_image = mod->Attribute(mod->imageset, "Imagefile");
    if (const auto file = FindNoCase(mods_folder / owner->folder, mod_image)) {
      mod_texture = ReadFile(*file);
    } else if (const auto in_pak = entries.find(PakKey(mod_image));
               in_pak == entries.end() || !pak->Read(in_pak->second, mod_texture, error)) {
      log.push_back(name + " (" + owner->folder + "): the mod's texture is missing; the mod's file is used");
      continue;
    }
    const std::string key = Upper(name);
    const std::string stem = key.substr(0, key.size() - std::string_view(".IMAGESET").size());
    uint64_t h = 0xCBF29CE484222325ull;
    h = Fnv(h, "icon atlas merge 1");
    h = Fnv(h, game_xml);
    h = Fnv(h, std::string_view(reinterpret_cast<const char*>(game_texture.data()), game_texture.size()));
    h = Fnv(h, mod_xml);
    h = Fnv(h, std::string_view(reinterpret_cast<const char*>(mod_texture.data()), mod_texture.size()));
    char hex[17];
    std::snprintf(hex, sizeof hex, "%016llX", static_cast<unsigned long long>(h));
    const std::string texture_key = stem + "." + hex + ".DDS";
    std::string why;
    const auto merged = MergeAtlases(game_xml, game_texture, mod_xml, mod_texture, device + "/" + texture_key, &why);
    if (!merged) {
      log.push_back(name + " (" + owner->folder + "): not merged, " + why);
      continue;
    }
    const std::filesystem::path xml_path = out / key, dds_path = out / texture_key;
    std::filesystem::create_directories(xml_path.parent_path(), ec);
    std::ofstream(xml_path, std::ios::binary).write(merged->xml.data(), std::streamsize(merged->xml.size()));
    std::ofstream(dds_path, std::ios::binary)
        .write(reinterpret_cast<const char*>(merged->dds.data()), std::streamsize(merged->dds.size()));
    if (!std::filesystem::is_regular_file(xml_path, ec) || !std::filesystem::is_regular_file(dds_path, ec)) {
      log.push_back(name + ": cannot write the merged files");
      continue;
    }
    written.push_back({key, owner->folder, merged->game_only, merged->from_mod});
  }
  return written;
}

}  // namespace torchlight::mods
