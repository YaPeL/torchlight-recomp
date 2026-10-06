// Our translations (data/ui/tl_*_strings.txt): a section per language code, "English text =
// translation" lines; "\n" in either side is a line break. Texts without one stay as they are.

#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace torchlight::game_menu {

class MenuStrings {
 public:
  // Parses the file's text; malformed lines are skipped and reported in `warnings`.
  void Parse(std::string_view text, std::vector<std::string>* warnings = nullptr);
  bool Load(const std::string& path, std::vector<std::string>& warnings, std::string& error);

  // Whether `language`'s section has a line for `english`.
  bool Has(const std::string& language, const std::string& english) const;
  // The language codes that have a section.
  std::vector<std::string> Languages() const;
  // `english` in `language`, or `english` itself when there is no translation.
  std::string Translate(const std::string& language, const std::string& english) const;

 private:
  std::map<std::string, std::map<std::string, std::string>> by_language_;
};

}  // namespace torchlight::game_menu
