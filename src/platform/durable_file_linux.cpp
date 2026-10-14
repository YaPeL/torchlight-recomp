// Linux: fsync writes the file's data and its metadata to the disk.

#include "platform/durable_file_posix.h"

#include <unistd.h>

namespace torchlight::platform {

bool FlushDescriptor(int descriptor) { return ::fsync(descriptor) == 0; }

}  // namespace torchlight::platform
