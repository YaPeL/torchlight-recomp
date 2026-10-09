// WindowsSeparators, ModsSearchPath and the mods' device names (guest_path.h): the paths the
// guest's directory search is given.
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

#include "hooks/guest_path.h"

using torchlight::hooks::ModsSearchPath;
using torchlight::hooks::WindowsSeparators;

namespace {
int failures = 0;
void Check(const char* in, std::optional<std::string> expected,
           std::optional<std::string> (*f)(std::string_view) = WindowsSeparators) {
  const auto got = f(in);
  if (got != expected) {
    ++failures;
    std::fprintf(stderr, "FAIL: \"%s\" -> \"%s\", expected \"%s\"\n", in, got ? got->c_str() : "(unchanged)",
                 expected ? expected->c_str() : "(unchanged)");
  }
}
}  // namespace

int main() {
  // What the game asks for a mod's subfolders (a CMod's folder with '/', plus "/*.*").
  Check("tlmod0:/tl_test_new_item//*.*", "tlmod0:\\tl_test_new_item\\*.*");
  Check("tlmod0:/tl_test_new_item/media/units//*.*", "tlmod0:\\tl_test_new_item\\media\\units\\*.*");
  Check("tlmod0:/tl_test_new_item/mod.dat", "tlmod0:\\tl_test_new_item\\mod.dat");
  // Mixed, and a doubled '\'.
  Check("tlmod0:\\tl_test_new_item\\/*", "tlmod0:\\tl_test_new_item\\*");
  Check("game:\\media\\\\units\\*.dat", "game:\\media\\units\\*.dat");
  // Already Windows paths: left alone, so the hook makes no copy.
  Check("tlmod0:\\tl_test_new_item\\*.*", std::nullopt);
  Check("SAVE:\\", std::nullopt);
  Check("*", std::nullopt);
  Check("", std::nullopt);
  // A leading "\\" (a device path) is kept; repeats after it are joined.
  Check("\\\\Device\\x", std::nullopt);
  Check("\\\\Device\\\\x", "\\\\Device\\x");
  Check("//Device/x", "\\\\Device\\x");
  // Only the mods' device: OGRE's recursive searches on game: ask for "<dir>/*" and stay as the
  // Xbox reads them (they never entered a subfolder there).
  Check("tlmod0:/tl_test_new_item//*.*", "tlmod0:\\tl_test_new_item\\*.*", ModsSearchPath);
  Check("TLMOD12:/x/*", "TLMOD12:\\x\\*", ModsSearchPath);
  Check("tlmod0:\\tl_test_new_item\\*.*", std::nullopt, ModsSearchPath);
  Check("game:\\RTShaderLib\\/*", std::nullopt, ModsSearchPath);
  Check("game:\\RTShaderLib\\materials/*", std::nullopt, ModsSearchPath);
  Check("SAVE:\\/*.*", std::nullopt, ModsSearchPath);
  Check("tlmod:/x/*", std::nullopt, ModsSearchPath);
  Check("tlmods:/x/*", std::nullopt, ModsSearchPath);
  Check("tlmod3x:/x/*", std::nullopt, ModsSearchPath);
  Check("tlmod7", std::nullopt, ModsSearchPath);
  Check("", std::nullopt, ModsSearchPath);
  // A mod's device name, and the paths recognized as on one.
  if (torchlight::hooks::ModDeviceLink(42) != "tlmod42:" ||
      !torchlight::hooks::OnModDevice(torchlight::hooks::ModDeviceLink(0) + "\\media") ||
      torchlight::hooks::OnModDevice("tlhost:\\x")) {
    ++failures;
    std::fprintf(stderr, "FAIL: mod device names\n");
  }
  if (failures) return EXIT_FAILURE;
  std::puts("guest_path_test: ok");
  return EXIT_SUCCESS;
}
