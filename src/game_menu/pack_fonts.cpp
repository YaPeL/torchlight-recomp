#include "game_menu/pack_fonts.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <system_error>

namespace torchlight::game_menu {

namespace fs = std::filesystem;

namespace {

bool EqualNoCase(std::string_view a, std::string_view b) {
  return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
           return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
         });
}

// A child of `dir` named `name` in any case (the game's paths ignore it).
std::optional<fs::path> Child(const fs::path& dir, std::string_view name) {
  std::error_code ec;
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    if (EqualNoCase(it->path().filename().string(), name)) return it->path();
  }
  return std::nullopt;
}

// Replaces the value of attribute `attribute` inside [begin, end) of `text`: the name after any
// white space, "=" (spaces around it allowed) and a value in double or single quotes; false without
// it.
bool ReplaceAttribute(std::string& text, size_t begin, size_t& end, std::string_view attribute,
                      std::string_view value) {
  const auto space = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
  for (size_t at = text.find(attribute, begin); at != std::string::npos && at < end;
       at = text.find(attribute, at + 1)) {
    if (at == 0 || !space(text[at - 1])) continue;
    size_t i = at + attribute.size();
    while (i < end && space(text[i])) ++i;
    if (i >= end || text[i] != '=') continue;
    ++i;
    while (i < end && space(text[i])) ++i;
    if (i >= end || (text[i] != '"' && text[i] != '\'')) continue;
    const char quote = text[i];
    const size_t start = i + 1;
    const size_t close = text.find(quote, start);
    if (close == std::string::npos || close >= end) return false;
    text.replace(start, close - start, value);
    end = end - (close - start) + value.size();
    return true;
  }
  return false;
}

}  // namespace

std::optional<std::string> DerivedFont(std::string_view font_xml, std::string_view name, int size) {
  std::string text(font_xml);
  const size_t begin = text.find("<Font");
  if (begin == std::string::npos) return std::nullopt;
  size_t end = text.find('>', begin);
  if (end == std::string::npos) return std::nullopt;
  if (!ReplaceAttribute(text, begin, end, "Name", name)) return std::nullopt;
  if (!ReplaceAttribute(text, begin, end, "Size", std::to_string(size))) return std::nullopt;
  return text;
}

bool WriteMissingSerif14(const fs::path& pack_dir, const fs::path& out_dir, std::string& error) {
  error.clear();
  // The pack's folder in any case too ("translations/RU/" for the language "ru").
  const std::optional<fs::path> pack = Child(pack_dir.parent_path(), pack_dir.filename().string());
  if (!pack) return false;
  const std::optional<fs::path> media = Child(*pack, "media");
  const std::optional<fs::path> ui = media ? Child(*media, "UI") : std::nullopt;
  if (!ui) return false;
  const std::optional<fs::path> serif = Child(*ui, "Serif.font");
  if (!serif || Child(*ui, "Serif14.font")) return false;
  std::ifstream in(*serif, std::ios::binary);
  const std::string xml((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const std::optional<std::string> serif14 = DerivedFont(xml, "Serif14", 14);
  if (!serif14) {
    error = serif->string() + " has no <Font Name=... Size=...>";
    return false;
  }
  std::error_code ec;
  fs::create_directories(out_dir / "media" / "UI", ec);
  std::ofstream out(out_dir / "media" / "UI" / "Serif14.font", std::ios::binary | std::ios::trunc);
  out << *serif14;
  out.close();
  if (!out) {
    error = "cannot write " + (out_dir / "media" / "UI" / "Serif14.font").string();
    return false;
  }
  return true;
}

}  // namespace torchlight::game_menu
