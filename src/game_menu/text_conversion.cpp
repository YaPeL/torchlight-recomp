#include "game_menu/text_conversion.h"

namespace torchlight::game_menu {

std::vector<uint32_t> Utf16ToCodePoints(std::u16string_view text) {
  std::vector<uint32_t> out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    const uint32_t unit = text[i];
    if (unit >= 0xD800 && unit <= 0xDBFF && i + 1 < text.size() && text[i + 1] >= 0xDC00 &&
        text[i + 1] <= 0xDFFF) {
      out.push_back(0x10000 + ((unit - 0xD800) << 10) + (uint32_t(text[i + 1]) - 0xDC00));
      ++i;
    } else if (unit >= 0xD800 && unit <= 0xDFFF) {
      out.push_back(0xFFFD);
    } else {
      out.push_back(unit);
    }
  }
  return out;
}

}  // namespace torchlight::game_menu
