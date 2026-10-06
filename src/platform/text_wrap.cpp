#include "platform/text_wrap.h"

namespace torchlight::platform {

namespace {

// Code points in a UTF-8 string: every byte but the continuation ones (10xxxxxx).
size_t Characters(std::string_view text) {
  size_t count = 0;
  for (unsigned char c : text) count += (c & 0xC0) != 0x80;
  return count;
}

// One line without line breaks, greedily filled word by word.
void WrapLine(std::string_view line, size_t width, std::string& out) {
  size_t used = 0;  // characters on the current output line
  size_t start = 0;
  while (start < line.size()) {
    size_t end = line.find(' ', start);
    if (end == std::string_view::npos) end = line.size();
    const std::string_view word = line.substr(start, end - start);
    start = end + 1;
    if (word.empty()) continue;  // runs of spaces
    const size_t length = Characters(word);
    if (used > 0 && used + 1 + length > width) {
      out += '\n';
      used = 0;
    }
    if (used > 0) {
      out += ' ';
      ++used;
    }
    out += word;
    used += length;
  }
}

}  // namespace

std::string WrapText(std::string_view text, size_t width) {
  std::string out;
  size_t start = 0;
  while (true) {
    const size_t end = text.find('\n', start);
    WrapLine(text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start),
             width, out);
    if (end == std::string_view::npos) break;
    out += '\n';
    start = end + 1;
  }
  return out;
}

}  // namespace torchlight::platform
