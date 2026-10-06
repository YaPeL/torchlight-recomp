// Tests for the video column's translations: parsing, lookups, and the shipped file.

#include <cstdio>
#include <cstdlib>
#include <string>

#include "game_menu/menu_strings.h"

namespace {

using torchlight::game_menu::MenuStrings;

void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    std::exit(1);
  }
}

void TestParse() {
  MenuStrings s;
  std::vector<std::string> warnings;
  s.Parse("# comment\nResolution = orphan\n[es]\nResolution = Resolución\r\nbroken line\n\n"
          "[fr]\n  Language   =   Langue  \nFrame Rate Limit = Limite d'images par seconde\n",
          &warnings);
  Check(s.Translate("es", "Resolution") == "Resolución", "Spanish, CRLF trimmed");
  Check(s.Translate("fr", "Language") == "Langue", "spaces trimmed");
  Check(s.Translate("fr", "Frame Rate Limit") == "Limite d'images par seconde", "apostrophe");
  Check(s.Translate("de", "Resolution") == "Resolution", "no section: English");
  Check(s.Translate("es", "1920x1080") == "1920x1080", "no line: unchanged");
  Check(s.Translate("en", "Resolution") == "Resolution", "English: unchanged");
  Check(warnings.size() == 2, "line before a section and the broken line reported");
}

void TestShippedFile() {
  MenuStrings s;
  std::vector<std::string> warnings;
  std::string error;
  Check(s.Load(TL_VIDEO_STRINGS_FILE, warnings, error), "shipped file loads");
  Check(warnings.empty(), "shipped file has no malformed lines");
  // Every label and word value the Video column shows (video_menu.cpp, video_menu_model.cpp).
  const char* texts[] = {"Video", "Resolution", "Aspect Ratio", "Frame Rate Limit", "Language",
                         "Achievements", "Vertical Sync", "Renderer", "GPU",
                         "Some changes need a restart", "Unlimited", "Window", "Default", "Auto",
                         "PC (incomplete)"};
  for (const char* language : {"de", "fr", "es"}) {
    for (const char* text : texts) {
      if (!s.Has(language, text)) {
        std::fprintf(stderr, "FAIL: %s has no line for %s\n", language, text);
        std::exit(1);
      }
    }
  }
  Check(s.Translate("es", "Unlimited") == "Sin límite", "Spanish value");
  Check(s.Translate("de", "Resolution") == "Auflösung", "German label");
  Check(s.Translate("fr", "Default") == "Par défaut", "French value");
}

}  // namespace

int main() {
  TestParse();
  TestShippedFile();
  std::printf("menu_strings_test: ok\n");
  return 0;
}
