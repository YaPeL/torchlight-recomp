#include "mods/text_encoding.h"

#include <algorithm>
#include <utility>

namespace torchlight::mods {

namespace {
constexpr size_t kMinTelling = 4;
}  // namespace

Utf16Order DetectUtf16Order(std::span<const uint8_t> bytes) {
  bool reversed_mark = false;
  if (bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
    bytes = bytes.subspan(2);
  } else if (bytes.size() >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF) {
    reversed_mark = true;
    bytes = bytes.subspan(2);
  }
  const size_t units = std::min(bytes.size() / 2, kUtf16SampleUnits);
  size_t big = 0, little = 0;
  for (size_t i = 0; i < units; ++i) {
    const uint8_t first = bytes[2 * i], second = bytes[2 * i + 1];
    if (first == 0 && second != 0) ++big;
    if (second == 0 && first != 0) ++little;
  }
  const size_t telling = big + little;
  if (telling < kMinTelling) return Utf16Order::kUnclear;
  if (big * 10 >= telling * 9) return Utf16Order::kBig;
  if (little * 10 >= telling * 9 && !reversed_mark) return Utf16Order::kLittle;
  return Utf16Order::kUnclear;
}

void SwapUtf16Units(std::span<uint8_t> bytes) {
  for (size_t i = 0; i + 1 < bytes.size(); i += 2) std::swap(bytes[i], bytes[i + 1]);
}

}  // namespace torchlight::mods
