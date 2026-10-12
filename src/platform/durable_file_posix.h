// Internal to durable_file: what each POSIX platform provides to durable_file_posix.cpp.

#pragma once

namespace torchlight::platform {

// Flushes an open file or folder to the disk; false with errno set.
bool FlushDescriptor(int descriptor);

}  // namespace torchlight::platform
