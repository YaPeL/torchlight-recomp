#include "mods/class_list.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <optional>

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

// A layout's text: UTF-8 or ASCII as it is, UTF-16 (with its byte order mark) narrowed; the names and
// properties looked for are ASCII.
std::string LayoutText(const std::vector<uint8_t>& bytes) {
  const bool le = bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE;
  const bool be = bytes.size() >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF;
  if (!le && !be) return std::string(bytes.begin(), bytes.end());
  std::string text;
  for (size_t i = 2; i + 1 < bytes.size(); i += 2) {
    const uint16_t unit = le ? uint16_t(bytes[i] | bytes[i + 1] << 8) : uint16_t(bytes[i] << 8 | bytes[i + 1]);
    text.push_back(unit < 0x80 ? char(unit) : '?');
  }
  return text;
}

std::vector<const PlannedMod*> EnabledInSearchOrder(const ModPlan& plan) {
  std::vector<const PlannedMod*> mods;
  for (const PlannedMod& mod : plan.mods) {
    if (mod.priority >= 0) mods.push_back(&mod);
  }
  return mods;
}

}  // namespace

std::vector<std::string> ClassButtons(std::string_view xml) {
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
      if (key && value && Upper(*key) == "ONCLICK" && Upper(*value) == "GUISELECT1") {
        names.push_back(*name);
        break;
      }
    }
  }
  return names;
}

std::vector<std::string> ClassList(const std::vector<std::vector<std::string>>& buttons_by_mod,
                                   const std::set<std::string>& player_units) {
  std::vector<std::string> list;
  std::set<std::string> seen;
  for (std::string_view game_class : kGameClasses) {
    list.emplace_back(game_class);
    seen.insert(Upper(game_class));
  }
  for (const auto& buttons : buttons_by_mod) {
    for (const std::string& name : buttons) {
      const std::string key = Upper(name);
      if (seen.contains(key) || !player_units.contains(key)) continue;
      seen.insert(key);
      list.push_back(name);
    }
  }
  return list;
}

std::set<std::string> ModPlayerUnits(const std::filesystem::path& mods_folder, const ModPlan& plan) {
  std::set<std::string> names;
  for (const PlannedMod* mod : EnabledInSearchOrder(plan)) {
    auto media = FindNoCase(mods_folder / mod->folder, "media");
    auto units = media ? FindNoCase(*media, "units") : std::nullopt;
    auto players = units ? FindNoCase(*units, "players") : std::nullopt;
    if (!players) continue;
    std::error_code ec;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(*players, ec)) {
      if (!entry.is_regular_file(ec) || Upper(entry.path().extension().string()) != ".DAT") continue;
      const std::vector<uint8_t> bytes = ReadFile(entry.path());
      std::optional<std::string> name;
      if (auto blocks = ParseDatText(bytes, nullptr); blocks && !blocks->empty()) {
        if (const DatValue* value = blocks->front().Find("NAME")) name = value->value;
      }
      if (!name) name = FindDatValueAnywhere(bytes, "NAME");
      if (name && !name->empty()) names.insert(Upper(*name));
    }
  }
  return names;
}

std::vector<std::vector<std::string>> ModClassButtons(const std::filesystem::path& mods_folder,
                                                      const ModPlan& plan) {
  std::vector<std::vector<std::string>> buttons;
  for (const PlannedMod* mod : EnabledInSearchOrder(plan)) {
    auto media = FindNoCase(mods_folder / mod->folder, "media");
    auto ui = media ? FindNoCase(*media, "ui") : std::nullopt;
    auto layout = ui ? FindNoCase(*ui, "charactercreate.layout") : std::nullopt;
    if (!layout) continue;
    buttons.push_back(ClassButtons(LayoutText(ReadFile(*layout))));
  }
  return buttons;
}

}  // namespace torchlight::mods
