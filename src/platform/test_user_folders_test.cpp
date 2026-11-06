// The probe cmake/check_test_user_folders.cmake runs outside ctest's folders: as a test executable it
// gets torchlight_test_user_folders like every other (top CMakeLists.txt), and prints the user
// folders it ended up with. By default it also writes a file in its cache folder, which must land
// in its own temporary folder; with --real_user_folders it only prints (the real folders are never
// written).

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "platform/test_user_folders.h"
#include "platform/user_folders.h"

namespace fs = std::filesystem;
using torchlight::platform::UserFolder;
using torchlight::platform::UserFolderKind;

int main(int argc, char** argv) {
  const bool real = argc > 1 && torchlight::platform::IsRealUserFoldersFlag(argv[1]);
  const char* variable = std::getenv(torchlight::platform::kTestUserFoldersVariable);
  const fs::path cache = UserFolder(UserFolderKind::kCache);
  std::printf("variable=%s\n", variable ? variable : "");
  std::printf("cache=%s\n", cache.generic_string().c_str());
  if (!real) {
    std::error_code error;
    fs::create_directories(cache, error);
    std::ofstream(cache / "probe.txt") << "probe\n";
    if (!fs::exists(cache / "probe.txt")) {
      std::fprintf(stderr, "FAIL: cannot write in %s\n", cache.string().c_str());
      return 1;
    }
  }
  return 0;
}
