// The test binaries' own user folders (test_user_folders.h): set up by a static object's constructor,
// so before main and before anything asks for a user folder. The command line is read from the
// operating system, since main's argv does not exist yet.

#include "platform/test_user_folders.h"

#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <system_error>
#include <vector>

#include "platform/user_folders.h"

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#elif defined(__APPLE__)
#include <crt_externs.h>
#include <unistd.h>
#else
#include <unistd.h>
#endif

namespace torchlight::platform {
namespace {

bool HasRealUserFoldersFlag() {
#if defined(_WIN32)
  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!argv) return false;
  bool found = false;
  for (int i = 1; i < argc; ++i) {
    if (std::wcscmp(argv[i], L"--real_user_folders") == 0) found = true;
  }
  LocalFree(argv);
  return found;
#elif defined(__APPLE__)
  const int argc = *_NSGetArgc();
  char** argv = *_NSGetArgv();
  for (int i = 1; i < argc; ++i) {
    if (IsRealUserFoldersFlag(argv[i])) return true;
  }
  return false;
#else
  // NUL-separated arguments, the program first.
  std::ifstream in("/proc/self/cmdline", std::ios::binary);
  std::string arg;
  bool first = true;
  while (std::getline(in, arg, '\0')) {
    if (!first && IsRealUserFoldersFlag(arg.c_str())) return true;
    first = false;
  }
  return false;
#endif
}

unsigned long ProcessId() {
#if defined(_WIN32)
  return GetCurrentProcessId();
#else
  return static_cast<unsigned long>(getpid());
#endif
}

void SetVariable(const char* name, const std::string& value) {
#if defined(_WIN32)
  _putenv_s(name, value.c_str());
#else
  setenv(name, value.c_str(), 1);
#endif
}

class TestUserFolders {
 public:
  TestUserFolders() {
    // Under ctest (or a test's child process, which inherits it): already isolated.
    if (const char* set = std::getenv(kTestUserFoldersVariable); set && *set) return;
    if (HasRealUserFoldersFlag()) return;
    std::error_code error;
    const std::filesystem::path temp = std::filesystem::temp_directory_path(error);
    std::random_device random;
    std::filesystem::path root = temp / ("TorchlightRecomp-test-" + std::to_string(ProcessId()) +
                                         "-" + std::to_string(random()));
    if (error || !std::filesystem::create_directories(root, error) || error) {
      // Without a folder of its own the test would use the real ones: it does not run.
      std::fprintf(stderr, "test user folders: cannot create %s; run with %s to use the real "
                   "user folders\n", root.string().c_str(), kRealUserFoldersFlag);
      std::_Exit(1);
    }
    root_ = root;
    SetVariable(kTestUserFoldersVariable, root.string());
  }
  ~TestUserFolders() {
    if (root_.empty()) return;
    std::error_code error;
    std::filesystem::remove_all(root_, error);
  }

 private:
  std::filesystem::path root_;  // created by this process, so removed by it
};

const TestUserFolders test_user_folders;

}  // namespace
}  // namespace torchlight::platform
