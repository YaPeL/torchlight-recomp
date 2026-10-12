// Linux and macOS: open, flush (durable_file_<platform>.cpp), and rename then flush the folder.

#include "platform/durable_file.h"
#include "platform/durable_file_posix.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <system_error>

namespace torchlight::platform {

namespace {

bool Flush(const std::filesystem::path& path, int flags, const char* what, std::string& error) {
  const int descriptor = ::open(path.c_str(), flags);
  if (descriptor < 0) {
    error = std::string("cannot open ") + what + " " + path.string() + " to flush it: " +
            std::strerror(errno);
    return false;
  }
  const bool flushed = FlushDescriptor(descriptor);
  const int flush_errno = errno;
  const bool closed = ::close(descriptor) == 0;
  if (!flushed || !closed) {
    error = std::string("cannot flush ") + what + " " + path.string() + ": " +
            std::strerror(flushed ? errno : flush_errno);
    return false;
  }
  return true;
}

}  // namespace

bool FlushFileToDisk(const std::filesystem::path& file, std::string& error) {
  return Flush(file, O_RDONLY | O_NOFOLLOW, "file", error);
}

bool CommitReplace(const std::filesystem::path& from, const std::filesystem::path& to,
                   std::string& error) {
  std::error_code ec;
  std::filesystem::rename(from, to, ec);
  if (ec) {
    error = "cannot rename " + from.string() + " to " + to.string() + ": " + ec.message();
    return false;
  }
  const std::filesystem::path folder = to.parent_path().empty() ? "." : to.parent_path();
  return Flush(folder, O_RDONLY | O_DIRECTORY, "folder", error);
}

}  // namespace torchlight::platform
