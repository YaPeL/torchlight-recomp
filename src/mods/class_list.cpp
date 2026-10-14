#include "mods/class_list.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>

#include "mods/dat_text.h"

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

// The NAMEs (ClassKey) of the units the enabled mods define under media/units/<group>/.
std::set<std::string> ModUnitNames(const std::filesystem::path& mods_folder, const ModPlan& plan, std::string_view group) {
  std::set<std::string> names;
  for (const PlannedMod* mod : EnabledInSearchOrder(plan)) {
    auto media = FindNoCase(mods_folder / mod->folder, "media");
    auto units = media ? FindNoCase(*media, "units") : std::nullopt;
    auto folder = units ? FindNoCase(*units, group) : std::nullopt;
    if (!folder) continue;
    std::error_code ec;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(*folder, ec)) {
      if (!entry.is_regular_file(ec) || Upper(entry.path().extension().string()) != ".DAT") continue;
      const std::vector<uint8_t> bytes = ReadFile(entry.path());
      std::optional<std::string> name;
      if (auto blocks = ParseDatText(bytes, nullptr); blocks && !blocks->empty()) {
        if (const DatValue* value = blocks->front().Find("NAME")) name = value->value;
      }
      if (!name) name = FindDatValueAnywhere(bytes, "NAME");
      if (name && !name->empty()) names.insert(ClassKey(*name));
    }
  }
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

std::vector<std::vector<std::string>> ModClassButtons(const std::filesystem::path& mods_folder, const ModPlan& plan) {
  return ModButtons(mods_folder, plan, ClassButtons);
}
std::vector<std::vector<std::string>> ModPetButtons(const std::filesystem::path& mods_folder, const ModPlan& plan) {
  return ModButtons(mods_folder, plan, PetButtons);
}

}  // namespace torchlight::mods
