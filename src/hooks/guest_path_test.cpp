// WindowsSeparators (guest_path.h): the paths the guest's directory search is given.
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>

#include "hooks/guest_path.h"

using torchlight::hooks::WindowsSeparators;

namespace {
int failures = 0;
void Check(const char* in, std::optional<std::string> expected) {
  const auto got = WindowsSeparators(in);
  if (got != expected) {
    ++failures;
    std::fprintf(stderr, "FAIL: \"%s\" -> \"%s\", expected \"%s\"\n", in, got ? got->c_str() : "(unchanged)",
                 expected ? expected->c_str() : "(unchanged)");
  }
}
}  // namespace

int main() {
  // What the game asks for a mod's subfolders (a CMod's folder with '/', plus "/*.*").
  Check("tlmods:/tl_test_new_item//*.*", "tlmods:\\tl_test_new_item\\*.*");
  Check("tlmods:/tl_test_new_item/media/units//*.*", "tlmods:\\tl_test_new_item\\media\\units\\*.*");
  Check("tlmods:/tl_test_new_item/mod.dat", "tlmods:\\tl_test_new_item\\mod.dat");
  // Mixed, and a doubled '\'.
  Check("tlmods:\\tl_test_new_item\\/*", "tlmods:\\tl_test_new_item\\*");
  Check("game:\\media\\\\units\\*.dat", "game:\\media\\units\\*.dat");
  // Already Windows paths: left alone, so the hook makes no copy.
  Check("tlmods:\\tl_test_new_item\\*.*", std::nullopt);
  Check("SAVE:\\", std::nullopt);
  Check("*", std::nullopt);
  Check("", std::nullopt);
  // A leading "\\" (a device path) is kept; repeats after it are joined.
  Check("\\\\Device\\x", std::nullopt);
  Check("\\\\Device\\\\x", "\\\\Device\\x");
  Check("//Device/x", "\\\\Device\\x");
  if (failures) return EXIT_FAILURE;
  std::puts("guest_path_test: ok");
  return EXIT_SUCCESS;
}
