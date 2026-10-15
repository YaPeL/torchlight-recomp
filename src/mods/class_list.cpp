#include "mods/class_list.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>

#include "mods/dat_text.h"
#include "save_import/pak.h"

namespace torchlight::mods {

namespace {

std::string Upper(std::string_view text) {
  std::string out(text);
  for (char& c : out) c = char(std::toupper(static_cast<unsigned char>(c)));
  return out;
}

// The value of attribute `name` in a start tag, or nothing.
std::optional<std::string> Attribute(std::string_view tag, std::string_view name) {
  const std::string key = std::string(name) + "=\"";
  const size_t at = tag.find(key);
  if (at == std::string_view::npos) return std::nullopt;
  const size_t begin = at + key.size();
  const size_t end = tag.find('"', begin);
  if (end == std::string_view::npos) return std::nullopt;
  return std::string(tag.substr(begin, end - begin));
}

std::vector<uint8_t> ReadFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// `dir`'s entry named `name` in any case, or nothing.
std::optional<std::filesystem::path> FindNoCase(const std::filesystem::path& dir, std::string_view name) {
  std::error_code ec;
  if (!std::filesystem::is_directory(dir, ec)) return std::nullopt;
  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    if (Upper(entry.path().filename().string()) == Upper(name)) return entry.path();
  }
  return std::nullopt;
}

std::vector<const PlannedMod*> EnabledInSearchOrder(const ModPlan& plan) {
  std::vector<const PlannedMod*> mods;
  for (const PlannedMod& mod : plan.mods) {
    if (mod.priority >= 0) mods.push_back(&mod);
  }
  return mods;
}

}  // namespace

std::string Utf16ToUtf8(std::u16string_view text) {
  std::string out;
  for (size_t i = 0; i < text.size(); ++i) {
    uint32_t c = text[i];
    if (c >= 0xD800 && c <= 0xDBFF && i + 1 < text.size() && text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF) {
      c = 0x10000 + ((c - 0xD800) << 10) + (text[++i] - 0xDC00);
    } else if (c >= 0xD800 && c <= 0xDFFF) {
      c = 0xFFFD;
    }
    if (c < 0x80) {
      out.push_back(char(c));
    } else if (c < 0x800) {
      out.push_back(char(0xC0 | c >> 6));
      out.push_back(char(0x80 | (c & 0x3F)));
    } else if (c < 0x10000) {
      out.push_back(char(0xE0 | c >> 12));
      out.push_back(char(0x80 | (c >> 6 & 0x3F)));
      out.push_back(char(0x80 | (c & 0x3F)));
    } else {
      out.push_back(char(0xF0 | c >> 18));
      out.push_back(char(0x80 | (c >> 12 & 0x3F)));
      out.push_back(char(0x80 | (c >> 6 & 0x3F)));
      out.push_back(char(0x80 | (c & 0x3F)));
    }
  }
  return out;
}

std::u16string Utf8ToUtf16(std::string_view text) {
  std::u16string out;
  for (size_t i = 0; i < text.size();) {
    const uint8_t b = uint8_t(text[i]);
    const int length = b < 0x80 ? 1 : (b >> 5) == 0x6 ? 2 : (b >> 4) == 0xE ? 3 : (b >> 3) == 0x1E ? 4 : 0;
    uint32_t c = length == 1 ? b : length == 2 ? b & 0x1F : length == 3 ? b & 0x0F : b & 0x07;
    bool valid = length > 0 && i + length <= text.size();
    for (int k = 1; valid && k < length; ++k) {
      const uint8_t next = uint8_t(text[i + k]);
      valid = (next & 0xC0) == 0x80;
      c = c << 6 | (next & 0x3F);
    }
    if (!valid || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) {
      out.push_back(u'\uFFFD');
      i += 1;
      continue;
    }
    if (c >= 0x10000) {
      c -= 0x10000;
      out.push_back(char16_t(0xD800 + (c >> 10)));
      out.push_back(char16_t(0xDC00 + (c & 0x3FF)));
    } else {
      out.push_back(char16_t(c));
    }
    i += size_t(length);
  }
  return out;
}

std::string ClassKey(std::string_view name) {
  std::u16string text = Utf8ToUtf16(name);
  for (char16_t& c : text) {
    if (c >= u'a' && c <= u'z') {
      c = char16_t(c - 0x20);
    } else if (c >= 0xE0 && c <= 0xFE && c != 0xF7) {
      c = char16_t(c - 0x20);
    } else if (c == 0xFF) {
      c = 0x178;
    }
  }
  return Utf16ToUtf8(text);
}

std::string LayoutText(const std::vector<uint8_t>& bytes) {
  const bool le = bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE;
  const bool be = bytes.size() >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF;
  if (le || be) {
    std::u16string text;
    for (size_t i = 2; i + 1 < bytes.size(); i += 2) {
      text.push_back(le ? char16_t(bytes[i] | bytes[i + 1] << 8) : char16_t(bytes[i] << 8 | bytes[i + 1]));
    }
    return Utf16ToUtf8(text);
  }
  const size_t skip = bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF ? 3 : 0;
  return std::string(bytes.begin() + skip, bytes.end());
}

namespace {

// The windows whose onClick is one of `commands` (upper case).
std::vector<std::string> ButtonsWithClick(std::string_view xml, std::initializer_list<std::string_view> commands) {
  std::vector<std::string> names;
  for (size_t pos = 0; (pos = xml.find("<Window", pos)) != std::string_view::npos; ++pos) {
    const size_t tag_end = xml.find('>', pos);
    if (tag_end == std::string_view::npos) break;
    const auto name = Attribute(xml.substr(pos, tag_end - pos), "Name");
    if (!name || xml[tag_end - 1] == '/') continue;
    // The window's own properties: up to its first child or its end.
    const size_t next_window = xml.find("<Window", tag_end);
    const size_t window_end = xml.find("</Window", tag_end);
    const size_t own_end = std::min(next_window, window_end);
    for (size_t p = tag_end; (p = xml.find("<Property", p)) != std::string_view::npos && p < own_end; ++p) {
      const size_t p_end = xml.find('>', p);
      if (p_end == std::string_view::npos) break;
      const std::string_view tag = xml.substr(p, p_end - p);
      const auto key = Attribute(tag, "Name");
      const auto value = Attribute(tag, "Value");
      if (key && value && Upper(*key) == "ONCLICK" &&
          std::find(commands.begin(), commands.end(), Upper(*value)) != commands.end()) {
        names.push_back(*name);
        break;
      }
    }
  }
  return names;
}

std::vector<std::string> ListOf(std::span<const std::string_view> game, const std::vector<std::vector<std::string>>& buttons_by_mod,
                                const std::set<std::string>& units) {
  std::vector<std::string> list;
  std::set<std::string> seen;
  for (std::string_view name : game) {
    list.emplace_back(name);
    seen.insert(ClassKey(name));
  }
  for (const auto& buttons : buttons_by_mod) {
    for (const std::string& name : buttons) {
      const std::string key = ClassKey(name);
      if (seen.contains(key) || !units.contains(key)) continue;
      seen.insert(key);
      list.push_back(name);
    }
  }
  return list;
}

// The units the enabled mods define under media/units/<group>/: each NAME (ClassKey) with the name
// the game shows for it (its DISPLAYNAME, else its NAME). A NAME defined twice keeps the first in
// the search order.
std::map<std::string, std::string> ModUnits(const std::filesystem::path& mods_folder, const ModPlan& plan,
                                            std::string_view group) {
  std::map<std::string, std::string> units;
  for (const PlannedMod* mod : EnabledInSearchOrder(plan)) {
    auto media = FindNoCase(mods_folder / mod->folder, "media");
    auto dir = media ? FindNoCase(*media, "units") : std::nullopt;
    auto folder = dir ? FindNoCase(*dir, group) : std::nullopt;
    if (!folder) continue;
    std::error_code ec;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(*folder, ec)) {
      if (!entry.is_regular_file(ec) || Upper(entry.path().extension().string()) != ".DAT") continue;
      const std::vector<uint8_t> bytes = ReadFile(entry.path());
      std::optional<std::string> name, shown;
      if (auto blocks = ParseDatText(bytes, nullptr); blocks && !blocks->empty()) {
        if (const DatValue* value = blocks->front().Find("NAME")) name = value->value;
        if (const DatValue* value = blocks->front().Find("DISPLAYNAME")) shown = value->value;
      } else {
        // A file the reader cannot parse: its first DISPLAYNAME line, as for the NAME.
        shown = FindDatValueAnywhere(bytes, "DISPLAYNAME");
      }
      if (!name) name = FindDatValueAnywhere(bytes, "NAME");
      if (!name || name->empty()) continue;
      units.try_emplace(ClassKey(*name), shown && !shown->empty() ? *shown : *name);
    }
  }
  return units;
}

std::set<std::string> ModUnitNames(const std::filesystem::path& mods_folder, const ModPlan& plan,
                                   std::string_view group) {
  std::set<std::string> names;
  for (const auto& [key, shown] : ModUnits(mods_folder, plan, group)) names.insert(key);
  return names;
}

std::vector<std::vector<std::string>> ModButtons(const std::filesystem::path& mods_folder, const ModPlan& plan,
                                                 std::vector<std::string> (*buttons_of)(std::string_view)) {
  std::vector<std::vector<std::string>> buttons;
  for (const PlannedMod* mod : EnabledInSearchOrder(plan)) {
    auto media = FindNoCase(mods_folder / mod->folder, "media");
    auto ui = media ? FindNoCase(*media, "ui") : std::nullopt;
    auto layout = ui ? FindNoCase(*ui, "charactercreate.layout") : std::nullopt;
    if (!layout) continue;
    buttons.push_back(buttons_of(LayoutText(ReadFile(*layout))));
  }
  return buttons;
}

}  // namespace

std::vector<std::string> ClassButtons(std::string_view xml) { return ButtonsWithClick(xml, {"GUISELECT1"}); }
std::vector<std::string> PetButtons(std::string_view xml) { return ButtonsWithClick(xml, {"GUIPET1", "GUISELECT2"}); }

std::vector<std::string> ClassList(const std::vector<std::vector<std::string>>& buttons_by_mod,
                                   const std::set<std::string>& player_units) {
  return ListOf(kGameClasses, buttons_by_mod, player_units);
}
std::vector<std::string> PetList(const std::vector<std::vector<std::string>>& buttons_by_mod,
                                 const std::set<std::string>& monster_units) {
  return ListOf(kGamePets, buttons_by_mod, monster_units);
}

std::set<std::string> ModPlayerUnits(const std::filesystem::path& mods_folder, const ModPlan& plan) {
  return ModUnitNames(mods_folder, plan, "players");
}
std::set<std::string> ModMonsterUnits(const std::filesystem::path& mods_folder, const ModPlan& plan) {
  return ModUnitNames(mods_folder, plan, "monsters");
}
std::map<std::string, std::string> ModMonsterDisplayNames(const std::filesystem::path& mods_folder,
                                                          const ModPlan& plan) {
  return ModUnits(mods_folder, plan, "monsters");
}

std::vector<std::vector<std::string>> ModClassButtons(const std::filesystem::path& mods_folder, const ModPlan& plan) {
  return ModButtons(mods_folder, plan, ClassButtons);
}
std::vector<std::vector<std::string>> ModPetButtons(const std::filesystem::path& mods_folder, const ModPlan& plan) {
  return ModButtons(mods_folder, plan, PetButtons);
}

namespace {

// "set:<imageset> image:<image>", either part optional in CEGUI; both required here.
bool ParseImageRef(const std::string& value, std::string& set, std::string& image) {
  const size_t s = value.find("set:");
  const size_t i = value.find("image:");
  if (s == std::string::npos || i == std::string::npos) return false;
  auto word = [&](size_t from) {
    size_t end = value.find(' ', from);
    return value.substr(from, end == std::string::npos ? std::string::npos : end - from);
  };
  set = word(s + 4);
  image = word(i + 6);
  return !set.empty() && !image.empty();
}

// The properties of the window whose start tag is at `pos` (its own, before any child window).
std::map<std::string, std::string> OwnProperties(std::string_view xml, size_t pos) {
  std::map<std::string, std::string> props;
  const size_t tag_end = xml.find('>', pos);
  if (tag_end == std::string_view::npos || xml[tag_end - 1] == '/') return props;
  const size_t own_end = std::min(xml.find("<Window", tag_end), xml.find("</Window", tag_end));
  for (size_t p = tag_end; (p = xml.find("<Property", p)) != std::string_view::npos && p < own_end; ++p) {
    const size_t p_end = xml.find('>', p);
    if (p_end == std::string_view::npos) break;
    const std::string_view tag = xml.substr(p, p_end - p);
    const auto key = Attribute(tag, "Name");
    const auto value = Attribute(tag, "Value");
    if (key && value) props[Upper(*key)] = *value;
  }
  return props;
}

}  // namespace

namespace {

// The images of the buttons whose onClick is one of `commands` (upper case), by ClassKey.
std::map<std::string, ButtonImages> ButtonImagesWithClick(std::string_view xml,
                                                         std::initializer_list<std::string_view> commands) {
  std::map<std::string, ButtonImages> out;
  for (size_t pos = 0; (pos = xml.find("<Window", pos)) != std::string_view::npos; ++pos) {
    const size_t tag_end = xml.find('>', pos);
    if (tag_end == std::string_view::npos) break;
    const auto name = Attribute(xml.substr(pos, tag_end - pos), "Name");
    if (!name) continue;
    const auto props = OwnProperties(xml, pos);
    const auto click = props.find("ONCLICK");
    if (click == props.end() ||
        std::find(commands.begin(), commands.end(), Upper(click->second)) == commands.end()) {
      continue;
    }
    auto image_of = [&](std::initializer_list<const char*> keys, std::string& set, std::string& image) {
      for (const char* key : keys) {
        if (const auto it = props.find(key); it != props.end()) return ParseImageRef(it->second, set, image);
      }
      return false;
    };
    ButtonImages images;
    std::string set;
    if (!image_of({"UNSELECTEDIMAGE", "NORMALIMAGE"}, images.imageset, images.normal)) continue;
    bool same = true;
    if (image_of({"HOVERIMAGE"}, set, images.hover)) same = same && set == images.imageset;
    if (image_of({"SELECTEDIMAGE", "PUSHEDIMAGE"}, set, images.pushed)) same = same && set == images.imageset;
    if (!same) continue;
    if (images.hover.empty()) images.hover = images.normal;
    if (images.pushed.empty()) images.pushed = images.normal;
    out.try_emplace(ClassKey(*name), images);
  }
  return out;
}

}  // namespace

std::map<std::string, ButtonImages> ClassButtonImages(std::string_view xml) {
  return ButtonImagesWithClick(xml, {"GUISELECT1"});
}
std::map<std::string, ButtonImages> PetButtonImages(std::string_view xml) {
  return ButtonImagesWithClick(xml, {"GUIPET1", "GUISELECT2"});
}

namespace {

// An attribute of a start tag, as an XML reader takes it: spaces around the '=' allowed
// (the game's own imagesets write `Name= "widgets"`), double or single quotes.
std::optional<std::string> XmlAttribute(std::string_view tag, std::string_view name) {
  for (size_t at = tag.find(name); at != std::string_view::npos; at = tag.find(name, at + 1)) {
    if (at == 0 || !std::isspace(static_cast<unsigned char>(tag[at - 1]))) continue;
    size_t p = at + name.size();
    while (p < tag.size() && std::isspace(static_cast<unsigned char>(tag[p]))) ++p;
    if (p >= tag.size() || tag[p] != '=') continue;
    ++p;
    while (p < tag.size() && std::isspace(static_cast<unsigned char>(tag[p]))) ++p;
    if (p >= tag.size() || (tag[p] != '"' && tag[p] != '\'')) continue;
    const size_t end = tag.find(tag[p], p + 1);
    if (end == std::string_view::npos) return std::nullopt;
    return std::string(tag.substr(p + 1, end - p - 1));
  }
  return std::nullopt;
}

}  // namespace

std::optional<ImagesetInfo> ParseImageset(std::string_view xml) {
  const size_t at = xml.find("<Imageset");
  if (at == std::string_view::npos) return std::nullopt;
  const size_t end = xml.find('>', at);
  if (end == std::string_view::npos) return std::nullopt;
  ImagesetInfo info;
  const std::string_view tag = xml.substr(at, end - at);
  const auto name = XmlAttribute(tag, "Name");
  const auto file = XmlAttribute(tag, "Imagefile");
  if (!name || name->empty() || !file || file->empty()) return std::nullopt;
  info.name = *name;
  info.image_file = *file;
  for (size_t pos = end; (pos = xml.find("<Image ", pos)) != std::string_view::npos; ++pos) {
    const size_t image_end = xml.find('>', pos);
    if (image_end == std::string_view::npos) break;
    if (const auto image = XmlAttribute(xml.substr(pos, image_end - pos), "Name")) info.images.insert(*image);
  }
  return info;
}

namespace {

// `relative` ('/' or '\\' separated) under `root`, each part matched in any case, or nothing.
std::optional<std::filesystem::path> FindPathNoCase(const std::filesystem::path& root, std::string relative) {
  std::replace(relative.begin(), relative.end(), '\\', '/');
  std::filesystem::path at = root;
  size_t from = 0;
  while (from <= relative.size()) {
    const size_t slash = relative.find('/', from);
    const std::string part = relative.substr(from, slash == std::string::npos ? std::string::npos : slash - from);
    if (!part.empty()) {
      const auto next = FindNoCase(at, part);
      if (!next) return std::nullopt;
      at = *next;
    }
    if (slash == std::string::npos) break;
    from = slash + 1;
  }
  std::error_code ec;
  return std::filesystem::is_regular_file(at, ec) ? std::optional(at) : std::nullopt;
}

// Whether an image file starts as a format the game's image codec reads: DDS, PNG, JPEG or BMP by
// their signatures, TGA (no signature) by its extension and its 18-byte header. Creating an imageset
// whose image does not decode throws inside the guest, which a call started from the host may not
// unwind cleanly; such an imageset is left out (its classes show the frame with the name).
bool ImageHeaderReadable(const std::filesystem::path& file) {
  std::ifstream in(file, std::ios::binary);
  char head[8] = {};
  in.read(head, sizeof(head));
  const std::string_view h(head, size_t(in.gcount()));
  if (h.starts_with("DDS ") || h.starts_with("\x89PNG\r\n\x1a\n") || h.starts_with("\xFF\xD8\xFF") ||
      h.starts_with("BM")) {
    return true;
  }
  std::error_code ec;
  return Upper(file.extension().string()) == ".TGA" && std::filesystem::file_size(file, ec) >= 18 && !ec;
}

}  // namespace

std::map<std::string, ModImageset> ModImagesets(const std::filesystem::path& mods_folder, const ModPlan& plan) {
  std::map<std::string, ModImageset> out;
  std::set<std::string> names, twice;
  for (const PlannedMod* mod : EnabledInSearchOrder(plan)) {
    const std::filesystem::path root = mods_folder / mod->folder;
    auto media = FindNoCase(root, "media");
    auto ui = media ? FindNoCase(*media, "ui") : std::nullopt;
    if (!ui) continue;
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(*ui, ec)) {
      if (entry.is_regular_file(ec) && Upper(entry.path().extension().string()) == ".IMAGESET") {
        files.push_back(entry.path());
      }
    }
    std::sort(files.begin(), files.end());
    for (const auto& file : files) {
      const auto info = ParseImageset(LayoutText(ReadFile(file)));
      if (!info) continue;
      if (!names.insert(info->name).second) {
        twice.insert(info->name);
        continue;
      }
      const auto image = FindPathNoCase(root, info->image_file);
      if (!image || !ImageHeaderReadable(*image)) continue;
      const std::u8string relative = file.lexically_relative(root).generic_u8string();
      out.emplace(info->name, ModImageset{mod->folder, std::string(relative.begin(), relative.end()), *info});
    }
  }
  for (const std::string& name : twice) out.erase(name);
  return out;
}

std::set<std::string> PakImagesetNames(const std::filesystem::path& pak, std::string* error) {
  std::set<std::string> names;
  std::string e;
  const auto archive = save_import::Pak::Open(pak, e);
  if (!archive) {
    if (error) *error = e;
    return names;
  }
  std::vector<uint32_t> entries;
  archive->ForEach([&](uint32_t index, const std::string& name) {
    if (name.starts_with("media/ui/") && name.ends_with(".imageset")) entries.push_back(index);
    return true;
  });
  for (const uint32_t index : entries) {
    std::vector<uint8_t> bytes;
    if (!archive->Read(index, bytes, e)) continue;
    if (const auto info = ParseImageset(LayoutText(bytes))) names.insert(info->name);
  }
  return names;
}

namespace {

std::map<std::string, ClassIcon> ModIcons(const std::filesystem::path& mods_folder, const ModPlan& plan,
                                          const std::map<std::string, ModImageset>& imagesets,
                                          std::map<std::string, ButtonImages> (*buttons)(std::string_view)) {
  std::map<std::string, ClassIcon> out;
  for (const PlannedMod* mod : EnabledInSearchOrder(plan)) {
    auto media = FindNoCase(mods_folder / mod->folder, "media");
    auto ui = media ? FindNoCase(*media, "ui") : std::nullopt;
    auto layout = ui ? FindNoCase(*ui, "charactercreate.layout") : std::nullopt;
    if (!layout) continue;
    for (const auto& [key, images] : buttons(LayoutText(ReadFile(*layout)))) {
      if (out.contains(key)) continue;
      const auto set = imagesets.find(images.imageset);
      if (set == imagesets.end()) continue;
      const auto& known = set->second.info.images;
      if (!known.contains(images.normal) || !known.contains(images.hover) || !known.contains(images.pushed)) continue;
      out.emplace(key, ClassIcon{images, set->second});
    }
  }
  return out;
}

}  // namespace

std::map<std::string, ClassIcon> ModClassIcons(const std::filesystem::path& mods_folder, const ModPlan& plan,
                                               const std::map<std::string, ModImageset>& imagesets) {
  return ModIcons(mods_folder, plan, imagesets, ClassButtonImages);
}
std::map<std::string, ClassIcon> ModPetIcons(const std::filesystem::path& mods_folder, const ModPlan& plan,
                                             const std::map<std::string, ModImageset>& imagesets) {
  return ModIcons(mods_folder, plan, imagesets, PetButtonImages);
}

}  // namespace torchlight::mods
