#include "live/busy_notice_text.h"

#include <algorithm>

namespace torchlight::live {

int BusyPercent(size_t done, size_t total) {
  if (total == 0) return 0;
  return static_cast<int>(std::min(done, total) * 100 / total);
}

std::string BusyNoticeLine(std::string_view text, size_t done, size_t total) {
  constexpr std::string_view kPercent = "{percent}";
  const std::string percent = std::to_string(BusyPercent(done, total));
  std::string line;
  size_t from = 0;
  for (size_t at; (at = text.find(kPercent, from)) != std::string_view::npos; from = at + kPercent.size()) {
    line.append(text.substr(from, at - from));
    line.append(percent);
  }
  line.append(text.substr(from));
  return line;
}

}  // namespace torchlight::live
