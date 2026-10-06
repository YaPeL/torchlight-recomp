// Tests that the executables run with the application manifest (torchlight.manifest): the UTF-8
// process code page, so a UTF-8 std::string names a file correctly through std::filesystem and the
// C runtime's narrow functions (checked by reading the names back with the wide API), and
// per-monitor DPI awareness from the start of the process. Also that a command-line argument with
// non-ASCII letters (a --game_data_root below a user folder such as Martín's, say) arrives as
// UTF-8 both ways the executables read it: main's argv (the replay, the tests) and
// GetCommandLineW converted to UTF-8 (the game: the SDK's wWinMain), the test running itself again
// with such an argument.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

namespace {

void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    std::exit(1);
  }
}

// Whether `dir` has an entry named exactly `name` (read with the wide API, so independent of the
// process code page).
bool HasEntry(const std::wstring& dir, const std::wstring& name) {
  WIN32_FIND_DATAW data;
  HANDLE find = FindFirstFileW((dir + L"\\*").c_str(), &data);
  if (find == INVALID_HANDLE_VALUE) return false;
  bool found = false;
  do {
    found = found || name == data.cFileName;
  } while (FindNextFileW(find, &data));
  FindClose(find);
  return found;
}

// D:\prueba-ñandú\Martín Pérez in UTF-8 and in UTF-16 (with a space: the argument is quoted).
const char kArgument[] = "D:\\prueba-\xC3\xB1" "and\xC3\xBA\\Mart\xC3\xADn P\xC3\xA9rez";
const wchar_t kWideArgument[] = L"D:\\prueba-\u00F1and\u00FA\\Mart\u00EDn P\u00E9rez";

std::string Utf8(const std::wstring& wide) {
  const int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), int(wide.size()), nullptr, 0,
                                       nullptr, nullptr);
  std::string utf8(size_t(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.data(), int(wide.size()), utf8.data(), size, nullptr,
                      nullptr);
  return utf8;
}

// The child: its argument read both ways, and as a path. Exit code 0 when all agree.
int Child(int argc, char** argv) {
  if (argc != 3 || std::string(argv[2]) != kArgument) return 3;  // main's argv (ANSI: UTF-8)
  int wargc = 0;
  wchar_t** wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
  const bool wide_ok = wargc == 3 && Utf8(wargv[2]) == kArgument;  // the SDK's way
  LocalFree(wargv);
  if (!wide_ok) return 4;
  if (std::filesystem::path(std::string(kArgument)).wstring() != kWideArgument) return 5;
  return 0;
}

void TestArguments() {
  wchar_t exe[MAX_PATH];
  Check(GetModuleFileNameW(nullptr, exe, MAX_PATH) > 0, "own path");
  std::wstring command = std::wstring(L"\"") + exe + L"\" --child \"" + kWideArgument + L"\"";
  STARTUPINFOW startup = {};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process = {};
  Check(CreateProcessW(exe, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup,
                       &process) != 0,
        "run the test again with a non-ASCII argument");
  WaitForSingleObject(process.hProcess, INFINITE);
  DWORD code = 1;
  GetExitCodeProcess(process.hProcess, &code);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  Check(code != 3, "main's argv has the argument in UTF-8");
  Check(code != 4, "GetCommandLineW, converted to UTF-8, has it too");
  Check(code == 0, "the UTF-8 argument names the same path as the wide one");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 1 && std::string(argv[1]) == "--child") return Child(argc, argv);
  Check(GetACP() == CP_UTF8, "the process code page is UTF-8 (the manifest's activeCodePage)");
  Check(AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(),
                                     DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2),
        "per-monitor DPI aware (V2) from the start (the manifest's dpiAwareness)");

  // "ñandú" in UTF-8 and in UTF-16 (escapes keep this file ASCII).
  const std::string name = "\xC3\xB1" "and\xC3\xBA";
  const std::wstring wide_name = L"ñandú";
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "tl_utf8_paths_test";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  // Narrow UTF-8 strings only from here on, as the code does with platform paths.
  const std::string dir = root.string() + "\\" + name;
  std::filesystem::create_directories(std::filesystem::path(dir));
  Check(HasEntry(root.wstring(), wide_name), "std::filesystem made the folder with its UTF-8 name");

  const std::string file = dir + "\\" + name + ".txt";
  FILE* out = std::fopen(file.c_str(), "wb");
  Check(out != nullptr, "fopen with a UTF-8 path");
  std::fputs("torchlight", out);
  std::fclose(out);
  Check(HasEntry(root.wstring() + L"\\" + wide_name, wide_name + L".txt"),
        "fopen made the file with its UTF-8 name");

  std::ifstream in(file);
  std::string text;
  std::getline(in, text);
  Check(text == "torchlight", "std::ifstream reads it back through the UTF-8 path");
  in.close();

  std::filesystem::remove_all(root);
  TestArguments();
  std::printf("utf8_paths_test: ok\n");
  return 0;
}
