// Tests for the UTF-16 to code point conversion that replaces the game's 8-bit one.

#include <cstdio>
#include <cstdlib>

#include "game_menu/text_conversion.h"

namespace {

using torchlight::game_menu::Utf16ToCodePoints;

void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    std::exit(1);
  }
}

}  // namespace

int main() {
  Check(Utf16ToCodePoints(u"Continue") ==
            std::vector<uint32_t>{'C', 'o', 'n', 't', 'i', 'n', 'u', 'e'},
        "ASCII unchanged");
  // Latin-1 letters (de, fr, es) come out as before: the old conversion kept them too.
  Check(Utf16ToCodePoints(u"Résolution ñ ü") ==
            std::vector<uint32_t>{'R', 0xE9, 's', 'o', 'l', 'u', 't', 'i', 'o', 'n', ' ', 0xF1, ' ', 0xFC},
        "Latin-1 letters kept");
  // Cyrillic: the old conversion kept only the low byte of each letter.
  const auto russian = Utf16ToCodePoints(u"Привет");
  Check(russian.size() == 6 && russian[0] == 0x41F && russian[1] == 0x440 && russian[5] == 0x442,
        "Cyrillic code points kept");
  Check(Utf16ToCodePoints(u"日本") == std::vector<uint32_t>{0x65E5, 0x672C}, "CJK (BMP)");
  Check(Utf16ToCodePoints(u"\U0001F600x") == std::vector<uint32_t>{0x1F600, 'x'},
        "surrogate pair combined");
  const char16_t lone[] = {0xD800, u'a', 0xDC00};
  Check(Utf16ToCodePoints(std::u16string_view(lone, 3)) ==
            std::vector<uint32_t>{0xFFFD, 'a', 0xFFFD},
        "lone surrogates replaced");
  Check(Utf16ToCodePoints(u"").empty(), "empty");
  std::printf("text_conversion_test: ok\n");
  return 0;
}
