// The font of the launcher's window and the install's progress window: Noto Sans, Latin subset (SIL
// Open Font License 1.1; third_party/fonts/noto-sans/README.md), embedded at build time
// (src/platform/CMakeLists.txt). Inside the platform module only.

#pragma once

#include <cstddef>

namespace torchlight::platform {

extern const unsigned char kLauncherFont[];
extern const size_t kLauncherFontSize;

}  // namespace torchlight::platform
