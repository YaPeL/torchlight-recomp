// DetectUtf16Order and SwapUtf16Units (text_encoding.h) on the starts of text data files.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "mods/text_encoding.h"

using namespace torchlight::mods;

namespace {
int failures = 0;
void Check(bool ok, const std::string& what) {
  if (!ok) {
    ++failures;
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
  }
}

// `text` as UTF-16 units in either order, after `mark` (the bytes FF FE, FE FF or none).
std::vector<uint8_t> Utf16(const std::u16string& text, bool little, std::vector<uint8_t> mark = {}) {
  std::vector<uint8_t> out = std::move(mark);
  for (char16_t c : text) {
    out.push_back(static_cast<uint8_t>(little ? c & 0xFF : c >> 8));
    out.push_back(static_cast<uint8_t>(little ? c >> 8 : c & 0xFF));
  }
  return out;
}
const std::vector<uint8_t> kFFFE = {0xFF, 0xFE}, kFEFF = {0xFE, 0xFF};
}  // namespace

int main() {
  const std::u16string unit = u"[UNIT]\r\n<STRING>UNIT_GUID:-7816429899798026672\r\n[/UNIT]\r\n";
  // PC's files: little-endian, with or without the mark.
  Check(DetectUtf16Order(Utf16(unit, true, kFFFE)) == Utf16Order::kLittle, "PC, UTF-16LE with mark");
  Check(DetectUtf16Order(Utf16(unit, true)) == Utf16Order::kLittle, "PC, UTF-16LE without mark");
  // A PC file starting with characters that are not ASCII (a comment in Chinese, then the data).
  Check(DetectUtf16Order(Utf16(u"中文\r\n" + unit, true, kFFFE)) == Utf16Order::kLittle,
        "PC, starting with non-ASCII characters");
  Check(DetectUtf16Order(Utf16(u"Épée\r\n" + unit, true)) == Utf16Order::kLittle,
        "PC, starting with accented Latin");
  // The Xbox game's own files: the bytes FF FE, then big-endian units (settings.txt).
  Check(DetectUtf16Order(Utf16(u"CONSOLE_ENABLED:0\r\nLEVEL_SEED:1\r\n", false, kFFFE)) == Utf16Order::kBig,
        "the Xbox game's FF FE + UTF-16BE");
  Check(DetectUtf16Order(Utf16(unit, false)) == Utf16Order::kBig, "UTF-16BE without mark");
  Check(DetectUtf16Order(Utf16(unit, false, kFEFF)) == Utf16Order::kBig, "UTF-16BE with its own mark");
  // Unclear: left as it is.
  Check(DetectUtf16Order(Utf16(u"中文字符串测试", true, kFFFE)) == Utf16Order::kUnclear,
        "no character below U+0100 in the sample");
  std::vector<uint8_t> mixed = Utf16(u"[UNIT]", true, kFFFE);
  for (uint8_t b : Utf16(u"[UNIT]", false)) mixed.push_back(b);
  Check(DetectUtf16Order(mixed) == Utf16Order::kUnclear, "half one order, half the other");
  const std::string utf8 = "[UNIT]\r\n<STRING>NAME:x\r\n";
  Check(DetectUtf16Order(std::vector<uint8_t>(utf8.begin(), utf8.end())) == Utf16Order::kUnclear, "UTF-8 or ANSI");
  Check(DetectUtf16Order(Utf16(u"[U", true, kFFFE)) == Utf16Order::kUnclear, "too short to tell");
  Check(DetectUtf16Order(Utf16(unit, true, kFEFF)) == Utf16Order::kUnclear, "a reversed mark on little-endian text");
  Check(DetectUtf16Order(std::vector<uint8_t>{}) == Utf16Order::kUnclear, "empty");
  // Only the first kUtf16SampleUnits units are looked at.
  std::u16string long_text(kUtf16SampleUnits, u'A');
  std::vector<uint8_t> tail = Utf16(long_text, true, kFFFE);
  for (uint8_t b : Utf16(u"garbage after the sample", false)) tail.push_back(b);
  Check(DetectUtf16Order(tail) == Utf16Order::kLittle, "judged on the sample only");

  // Turned around, a PC file is the big-endian file the guest reads, mark included (FF FE becomes
  // FE FF, which the reader has already skipped as 0xFFFE).
  std::vector<uint8_t> pc = Utf16(unit, true, kFFFE);
  SwapUtf16Units(pc);
  Check(pc == Utf16(unit, false, kFEFF), "swapped units equal the big-endian file");
  std::vector<uint8_t> odd = {0x5B, 0x00, 0x41};
  SwapUtf16Units(odd);
  Check((odd == std::vector<uint8_t>{0x00, 0x5B, 0x41}), "a trailing odd byte is left as it is");

  if (failures) return EXIT_FAILURE;
  std::puts("text_encoding_test: ok");
  return EXIT_SUCCESS;
}
