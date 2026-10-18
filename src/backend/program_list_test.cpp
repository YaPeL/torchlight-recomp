// ReadProgramList / AppendProgram: the list of generated programs in the shader cache folder.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "backend/program_list.h"

namespace {
int g_failures = 0;
void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++g_failures;
  }
}
}  // namespace

int main() {
  using torchlight::backend::AppendProgram;
  using torchlight::backend::ReadProgramList;
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "tl_program_list_test";
  fs::remove_all(dir);
  const fs::path list = dir / "sub" / "programs.txt";
  Check(ReadProgramList(list, 4).empty(), "a missing file is an empty list");

  const std::string a("\x01\x02\xfe\x00", 4), b("\xff\x10\x20\x30", 4);
  Check(AppendProgram(list, a, 4, true), "the first append writes the header and the folder");
  Check(AppendProgram(list, b, 4, false), "a later append adds a line");
  Check(!AppendProgram(list, std::string(3, 'x'), 4, false), "a key of another size is refused");
  auto keys = ReadProgramList(list, 4);
  Check(keys.size() == 2 && keys[0] == a && keys[1] == b, "the keys read back, oldest first");

  Check(ReadProgramList(list, 5).empty(), "another key size makes the list empty");
  {
    std::ofstream out(list, std::ios::app);
    out << "zz02fe00\n0102\n";  // not hex, too short
  }
  Check(ReadProgramList(list, 4).size() == 2, "lines that are not keys are skipped");

  Check(AppendProgram(list, b, 4, true), "a fresh append starts the file again");
  keys = ReadProgramList(list, 4);
  Check(keys.size() == 1 && keys[0] == b, "the old keys are gone after a fresh start");
  {
    std::ofstream out(list, std::ios::trunc);
    out << "tl-programs 0 4\n01020304\n";
  }
  Check(ReadProgramList(list, 4).empty(), "another format makes the list empty");
  fs::remove_all(dir);
  return g_failures ? 1 : 0;
}
