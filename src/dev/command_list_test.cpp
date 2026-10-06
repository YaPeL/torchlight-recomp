// --dev_guest_command parsing: steps, same-frame groups, spaces and empty entries.
#include "dev/command_list.h"
#include <cstdlib>
#include <iostream>
namespace {
int failures = 0;
void Check(bool value, const char* message) {
  if (!value) { ++failures; std::cerr << message << '\n'; }
}
using Steps = std::vector<std::vector<std::string>>;
}  // namespace
int main() {
  using torchlight::dev::ParseCommandList;
  Check(ParseCommandList("").empty(), "empty value has no steps");
  Check(ParseCommandList("FAME 2500000") == Steps{{"FAME 2500000"}}, "one command");
  Check(ParseCommandList(" DESCEND ; DESCEND 47 ") == Steps{{"DESCEND"}, {"DESCEND 47"}},
        "steps split on ';' and trimmed");
  Check(ParseCommandList("GOD & DUNGEON RANDOMDUNGEON & DESCEND;ASCEND") ==
            Steps{{"GOD", "DUNGEON RANDOMDUNGEON", "DESCEND"}, {"ASCEND"}},
        "'&' groups commands of one step");
  Check(ParseCommandList(";; & ;DESCEND&&") == Steps{{"DESCEND"}}, "empty commands and steps skipped");
  if (failures) return EXIT_FAILURE;
  std::cout << "dev command list tests passed\n";
  return EXIT_SUCCESS;
}
