// Tests for durable_file.h on this platform: a file written, flushed and committed over an older
// one reads back whole; a missing file or rename fails with a reason and leaves things as they were.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "platform/durable_file.h"

namespace {

namespace fs = std::filesystem;
using torchlight::platform::CommitReplace;
using torchlight::platform::FlushFileToDisk;

int failures = 0;
void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

void Write(const fs::path& file, const std::string& text) {
  std::ofstream(file, std::ios::binary | std::ios::trunc) << text;
}

std::string Read(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

}  // namespace

int main() {
  const fs::path root = fs::temp_directory_path() / "tl_durable_file_test";
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root);
  const fs::path target = root / "settings.toml";
  const fs::path temporary = root / "settings.toml.tmp";
  std::string error;

  // A new file, then a replacement of it.
  Write(temporary, "first");
  Check(FlushFileToDisk(temporary, error), "flush a new file");
  Check(CommitReplace(temporary, target, error), "commit a new file");
  Check(Read(target) == "first" && !fs::exists(temporary), "the new file is in place");
  Write(temporary, "second, longer");
  Check(FlushFileToDisk(temporary, error) && CommitReplace(temporary, target, error),
        "flush and commit over the old file");
  Check(Read(target) == "second, longer" && !fs::exists(temporary), "the old file is replaced");

  // Failures: a reason, and the target untouched.
  error.clear();
  Check(!FlushFileToDisk(root / "missing", error) && !error.empty(), "flush of a missing file");
  error.clear();
  Check(!CommitReplace(root / "missing", target, error) && !error.empty(),
        "commit of a missing file");
  Check(Read(target) == "second, longer", "a failed commit leaves the target");

  fs::remove_all(root, ec);
  if (failures) return 1;
  std::printf("durable file test: ok\n");
  return 0;
}
