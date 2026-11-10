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

// Replaces the value of attribute `attribute` inside [begin, end) of `text`; false without it.
bool ReplaceAttribute(std::string& text, size_t begin, size_t& end, std::string_view attribute,
                      std::string_view value) {
  const std::string key = " " + std::string(attribute) + "=\"";
  const size_t at = text.find(key, begin);
  if (at == std::string::npos || at >= end) return false;
  const size_t start = at + key.size();
  const size_t close = text.find('"', start);
  if (close == std::string::npos || close >= end) return false;
  text.replace(start, close - start, value);
  end = end - (close - start) + value.size();
  return true;
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
  const std::optional<fs::path> media = Child(pack_dir, "media");
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
