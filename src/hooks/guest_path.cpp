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

std::string ModDeviceLink(size_t index) { return "tlmod" + std::to_string(index) + ":"; }

bool OnModDevice(std::string_view path) {
  static constexpr std::string_view kPrefix = "tlmod";
  if (path.size() < kPrefix.size() + 2) return false;
  for (size_t i = 0; i < kPrefix.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(path[i])) != kPrefix[i]) return false;
  }
  size_t at = kPrefix.size();
  while (at < path.size() && std::isdigit(static_cast<unsigned char>(path[at]))) ++at;
  return at > kPrefix.size() && at < path.size() && path[at] == ':';
}

std::optional<std::string> ModsSearchPath(std::string_view path) {
  if (!OnModDevice(path)) return std::nullopt;
  return WindowsSeparators(path);
}

}  // namespace torchlight::hooks
