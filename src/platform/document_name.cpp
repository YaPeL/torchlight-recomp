#include "platform/document_name.h"

#include <algorithm>

namespace torchlight::platform {

namespace {

int HexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

}  // namespace

std::string PickedDocumentName(const std::string& uri) {
  std::string segment = uri.substr(uri.find_last_of('/') + 1);
  segment = segment.substr(0, segment.find_first_of("?#"));
  std::string decoded;
  for (size_t i = 0; i < segment.size(); ++i) {
    if (segment[i] == '%' && i + 2 < segment.size() && HexDigit(segment[i + 1]) >= 0 &&
        HexDigit(segment[i + 2]) >= 0) {
      decoded += char(HexDigit(segment[i + 1]) * 16 + HexDigit(segment[i + 2]));
      i += 2;
    } else {
      decoded += segment[i];
    }
  }
  const std::string name = decoded.substr(decoded.find_last_of("/:") + 1);
  const bool plain = name.find('.') != std::string::npos && name.front() != '.' &&
                     name.find('\\') == std::string::npos &&
                     std::none_of(name.begin(), name.end(),
                                  [](char c) { return static_cast<unsigned char>(c) < 0x20; });
  return plain ? name : "picked_file";
}

}  // namespace torchlight::platform
