#include "game_menu/menu_strings.h"

#include <fstream>
#include <sstream>

namespace torchlight::game_menu {

namespace {
// "\n" (backslash, n) to a line break.
std::string Unescape(std::string_view s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'n') {
      out += '\n';
      ++i;
    } else {
      out += s[i];
    }
  }
  return out;
}

std::string_view Trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
  return s;
}
}  // namespace

void MenuStrings::Parse(std::string_view text, std::vector<std::string>* warnings) {
  std::string section;
  int line_number = 0;
  while (!text.empty()) {
    const size_t eol = text.find('\n');
    std::string_view line = Trim(text.substr(0, eol));
    text = eol == std::string_view::npos ? std::string_view{} : text.substr(eol + 1);
    ++line_number;
    if (line.empty() || line.front() == '#') continue;
    if (line.front() == '[' && line.back() == ']') {
      section = std::string(Trim(line.substr(1, line.size() - 2)));
      continue;
    }
    const size_t eq = line.find(" = ");
    if (section.empty() || eq == std::string_view::npos) {
      if (warnings) warnings->push_back("line " + std::to_string(line_number) + " skipped");
      continue;
    }
    by_language_[section][Unescape(Trim(line.substr(0, eq)))] =
        Unescape(Trim(line.substr(eq + 3)));
  }
}

bool MenuStrings::Load(const std::string& path, std::vector<std::string>& warnings,
                       std::string& error) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    error = "cannot open " + path;
    return false;
  }
  std::ostringstream text;
  text << in.rdbuf();
  Parse(text.str(), &warnings);
  return true;
}

std::string MenuStrings::Translate(const std::string& language, const std::string& english) const {
  auto section = by_language_.find(language);
  if (section == by_language_.end()) return english;
  auto it = section->second.find(english);
  return it == section->second.end() ? english : it->second;
}

std::vector<std::string> MenuStrings::Languages() const {
  std::vector<std::string> languages;
  for (const auto& [language, texts] : by_language_) languages.push_back(language);
  return languages;
}

bool MenuStrings::Has(const std::string& language, const std::string& english) const {
  auto section = by_language_.find(language);
  return section != by_language_.end() && section->second.count(english) != 0;
}

}  // namespace torchlight::game_menu
