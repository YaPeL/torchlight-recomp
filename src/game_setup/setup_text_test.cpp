// Tests for the first start's texts: every English text (setup_text.h, game_files.h, install.h)
// has a translation in each language of tl_setup_strings.txt with the same {placeholders}, the
// file has no line for a text that does not exist, messages render, and the language is chosen.

#include <cstdio>
#include <cstdlib>
#include <regex>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "game_menu/menu_strings.h"
#include "game_setup/game_files.h"
#include "game_setup/install.h"
#include "game_setup/setup_text.h"

namespace {

using namespace torchlight::game_setup;

int failures = 0;
void Check(bool ok, const std::string& what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    ++failures;
  }
}

std::vector<std::string> AllTexts() {
  std::vector<std::string> texts;
  for (std::string_view t : InterfaceTexts()) texts.emplace_back(t);
  for (std::string_view t :
       {kTextNotPackage, kTextNotArcade, kTextNotTorchlight, kTextMissing, kTextOtherSize,
        kTextDiffers, kTextCannotRead, kTextCannotReadFolder, kTextCannotCopy,
        kTextCannotReadPackage, kTextCannotReadEntry, kTextCannotWrite, kTextCannotCreate,
        kTextCannotMoveAside, kTextCannotFinish}) {
    texts.emplace_back(t);
  }
  return texts;
}

std::multiset<std::string> Placeholders(const std::string& text) {
  std::multiset<std::string> found;
  const std::regex placeholder(R"(\{[a-z_]+\})");
  for (auto it = std::sregex_iterator(text.begin(), text.end(), placeholder);
       it != std::sregex_iterator(); ++it) {
    found.insert(it->str());
  }
  return found;
}

// The English texts of the file (the left side of each line), per section.
std::set<std::string> FileKeys(const torchlight::game_menu::MenuStrings& strings,
                               const std::string& language, const std::vector<std::string>& known) {
  std::set<std::string> keys;
  for (const std::string& text : known) {
    if (strings.Has(language, text)) keys.insert(text);
  }
  return keys;
}

}  // namespace

int main() {
  torchlight::game_menu::MenuStrings strings;
  std::vector<std::string> warnings;
  std::string error;
  Check(strings.Load(TL_SETUP_STRINGS_FILE, warnings, error), "strings file loads: " + error);
  Check(warnings.empty(), "no malformed lines");
  const std::vector<std::string> texts = AllTexts();
  const std::vector<std::string> languages = {"de", "fr", "es"};
  Check(strings.Languages() == std::vector<std::string>{"de", "es", "fr"}, "sections de, es, fr");
  for (const std::string& language : languages) {
    for (const std::string& english : texts) {
      const std::string translated = strings.Translate(language, english);
      Check(strings.Has(language, english),
            language + " has no line for: " + english.substr(0, 60));
      Check(Placeholders(translated) == Placeholders(english),
            language + " changes the placeholders of: " + english.substr(0, 60));
    }
    Check(FileKeys(strings, language, texts).size() == texts.size(), language + ": every text");
  }
  // The file has no line for a text the code does not use: count its lines per section.
  {
    std::FILE* file = std::fopen(TL_SETUP_STRINGS_FILE, "r");
    char line[4096];
    std::string section;
    std::map<std::string, size_t> lines;
    while (file && std::fgets(line, sizeof(line), file)) {
      const std::string s(line);
      if (s.starts_with("[")) section = s.substr(1, s.find(']') - 1);
      else if (!section.empty() && s.find(" = ") != std::string::npos) ++lines[section];
    }
    if (file) std::fclose(file);
    for (const std::string& language : languages) {
      Check(lines[language] == texts.size(),
            language + ": a line for a text the code does not use");
    }
  }

  // Rendering: translation, then the placeholders; "\n" in the file is a line break.
  const Translate spanish = [&](const std::string& english) {
    return strings.Translate("es", english);
  };
  const std::string missing = Render({std::string(kTextMissing), {{"file", "pak.zip"}}}, spanish);
  Check(missing.find("pak.zip") != std::string::npos && missing.find("{file}") == std::string::npos,
        "placeholders filled in the translation");
  Check(Render({std::string(kTextMissing), {{"file", "pak.zip"}}}) ==
            "The game files are incomplete: pak.zip is missing.",
        "English without a translation");
  Check(Render({std::string(kTextIntro), {{"where", "/x"}}}, spanish).find("\n\n") !=
            std::string::npos,
        "line breaks in the translation");

  // The language.
  const std::vector<std::string> available = {"de", "es", "fr"};
  Check(SetupLanguage("fr", std::string("de"), {"es"}, available) == "fr", "settings first");
  Check(SetupLanguage("", std::string("de"), {"es"}, available) == "de", "then --user_language");
  Check(SetupLanguage("", std::nullopt, {"pt-br", "pt", "es-ar", "es"}, available) == "es",
        "then the first preferred language with texts");
  Check(SetupLanguage("", std::nullopt, {"ja"}, available) == "en", "else English");

  if (failures) return EXIT_FAILURE;
  std::printf("setup_text_test: ok\n");
  return EXIT_SUCCESS;
}
