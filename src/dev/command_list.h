// Development only (guest_command.h): the --dev_guest_command value as steps. Steps are separated by
// ';' and run one per game level load; the commands of a step are separated by '&' and run in the
// same frame, for commands that load nothing (DUNGEON) followed by one that does (DESCEND).
// Surrounding spaces are dropped and empty commands and steps are skipped.

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace torchlight::dev {

inline std::vector<std::vector<std::string>> ParseCommandList(std::string_view value) {
  const auto split = [](std::string_view text, char separator) {
    std::vector<std::string_view> parts;
    size_t start = 0;
    while (start <= text.size()) {
      size_t end = text.find(separator, start);
      if (end == std::string_view::npos) end = text.size();
      parts.push_back(text.substr(start, end - start));
      start = end + 1;
    }
    return parts;
  };
  std::vector<std::vector<std::string>> steps;
  for (const auto step_text : split(value, ';')) {
    std::vector<std::string> step;
    for (const auto command : split(step_text, '&')) {
      const size_t first = command.find_first_not_of(' ');
      if (first == std::string_view::npos) continue;
      const size_t last = command.find_last_not_of(' ');
      step.emplace_back(command.substr(first, last - first + 1));
    }
    if (!step.empty()) steps.push_back(std::move(step));
  }
  return steps;
}

}  // namespace torchlight::dev
