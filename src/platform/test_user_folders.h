// The test binaries' own user folders (test_user_folders.cpp). Every test executable links it (the
// top CMakeLists.txt adds it to each one): before main, when ctest did not set
// kTestUserFoldersVariable (user_folders.h), it points that variable at a new temporary folder of
// the process's own, removed at exit, so a test run by hand never writes to the user's real
// folders. Only with kRealUserFoldersFlag on its command line does a test binary use the real ones.

#pragma once

#include <cstring>

namespace torchlight::platform {

inline constexpr const char* kRealUserFoldersFlag = "--real_user_folders";

// For the test binaries that parse their command line: the flag is the module's, not theirs.
inline bool IsRealUserFoldersFlag(const char* arg) {
  return std::strcmp(arg, kRealUserFoldersFlag) == 0;
}

}  // namespace torchlight::platform
