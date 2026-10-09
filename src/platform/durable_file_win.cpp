// Windows: FlushFileBuffers writes the file's data to the disk; MoveFileExW with
// MOVEFILE_WRITE_THROUGH returns only once the rename is on the disk (no folder to flush).

#include "platform/durable_file.h"

#include <windows.h>

#include <system_error>

namespace torchlight::platform {

namespace {

std::string LastError() { return std::system_category().message(int(GetLastError())); }

}  // namespace

bool FlushFileToDisk(const std::filesystem::path& file, std::string& error) {
  HANDLE handle = CreateFileW(file.c_str(), GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    error = "cannot open file " + file.string() + " to flush it: " + LastError();
    return false;
  }
  const bool flushed = FlushFileBuffers(handle) != 0;
  const std::string reason = flushed ? "" : LastError();
  CloseHandle(handle);
  if (!flushed) {
    error = "cannot flush file " + file.string() + ": " + reason;
    return false;
  }
  return true;
}

bool CommitReplace(const std::filesystem::path& from, const std::filesystem::path& to,
                   std::string& error) {
  if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    error = "cannot rename " + from.string() + " to " + to.string() + ": " + LastError();
    return false;
  }
  return true;
}

}  // namespace torchlight::platform
