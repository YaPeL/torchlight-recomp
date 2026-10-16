// Tests for the busy notice's line (busy_notice_text.h): the percent, its rounding and the
// placeholder; every text translated in each language of tl_import_strings.txt, the placeholder
// kept.

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "game_menu/menu_strings.h"
#include "live/busy_notice_text.h"

namespace {

using namespace torchlight::live;

void Check(bool ok, const std::string& what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    std::exit(1);
  }
}

}  // namespace

int main() {
  Check(BusyPercent(0, 1984) == 0, "nothing done: 0");
  Check(BusyPercent(833, 1984) == 41, "rounded down");
  Check(BusyPercent(1983, 1984) == 99, "not 100 before the end");
  Check(BusyPercent(1984, 1984) == 100, "all done: 100");
  Check(BusyPercent(5, 0) == 0, "no total: 0");
  Check(BusyPercent(9, 4) == 100, "past the total: 100");
  Check(BusyNoticeLine("Preparing mods... {percent}%", 992, 1984) == "Preparing mods... 50%", "English");
  Check(BusyNoticeLine("Mods werden vorbereitet... {percent} %", 1, 3) == "Mods werden vorbereitet... 33 %",
        "the placeholder anywhere");
  Check(BusyNoticeLine("{percent}/{percent}", 1, 2) == "50/50", "every placeholder");
  Check(BusyNoticeLine("No placeholder", 1, 2) == "No placeholder", "no placeholder");
  Check(BusyNoticeLine("{percent", 1, 2) == "{percent", "an unclosed brace stays");
  torchlight::game_menu::MenuStrings strings;
  std::vector<std::string> warnings;
  std::string error;
  Check(strings.Load(TL_IMPORT_STRINGS_FILE, warnings, error), "loads: " + error);
  for (const std::string& language : strings.Languages()) {
    Check(strings.Has(language, kPreparingModsText), std::string("translated in ") + language);
    Check(strings.Translate(language, kPreparingModsText).find("{percent}") != std::string::npos,
          std::string("the placeholder kept in ") + language);
  }
  Check(strings.Languages().size() >= 3, "de, fr, es");
  std::puts("busy_notice_text_test: ok");
  return 0;
}
