#include "hooks/guest_path.h"

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

}  // namespace torchlight::hooks
