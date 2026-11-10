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

// A file name with an extension that cannot be read as a path, hidden or a control character.
bool Plain(const std::string& name) {
  return name.size() <= kMaxDocumentName && name.find('.') != std::string::npos && name.front() != '.' && name.find('\\') == std::string::npos &&
         std::none_of(name.begin(), name.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20; });
}

}  // namespace

std::string PickedDocumentName(const std::string& uri, const std::string& display_name) {
  if (display_name.find_first_of("/:") == std::string::npos && Plain(display_name)) return display_name;
  return PickedDocumentName(uri);
}

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
  return Plain(name) ? name : "picked_file";
}

}  // namespace torchlight::platform
