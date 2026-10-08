#include "hooks/guest_path.h"

#include <cctype>

namespace torchlight::hooks {

std::optional<std::string> WindowsSeparators(std::string_view path) {
  std::string out;
  out.reserve(path.size());
  for (size_t i = 0; i < path.size(); ++i) {
    const char c = path[i] == '/' ? '\\' : path[i];
    if (c == '\\' && i > 1 && out.back() == '\\') continue;
    if (c == '\\' && i == 1 && out.back() == '\\') {
      out += c;  // a leading "\\" stays as it is
      continue;
    }
    out += c;
  }
  if (out == path) return std::nullopt;
  return out;
}

std::optional<std::string> ModsSearchPath(std::string_view path) {
  static constexpr std::string_view kDevice = "tlmods:";
  if (path.size() < kDevice.size()) return std::nullopt;
  for (size_t i = 0; i < kDevice.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(path[i])) != kDevice[i]) return std::nullopt;
  }
  return WindowsSeparators(path);
}

}  // namespace torchlight::hooks
