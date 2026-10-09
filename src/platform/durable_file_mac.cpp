// macOS: fsync only hands the data to the drive, which may keep it in its cache; F_FULLFSYNC asks
// the drive to write it (fsync(2), fcntl(2)). Volumes that do not support it (some network and
// FAT ones) answer ENOTSUP or EINVAL: fsync is what they have.

#include "platform/durable_file_posix.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>

namespace torchlight::platform {

bool FlushDescriptor(int descriptor) {
  if (::fcntl(descriptor, F_FULLFSYNC) == 0) return true;
  if (errno != ENOTSUP && errno != EINVAL) return false;
  return ::fsync(descriptor) == 0;
}

}  // namespace torchlight::platform
