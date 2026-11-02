// Which operating system this build is for, for texts that differ between them (the setup's
// explanations). A header alone, with no platform code: libraries without the platform module use it.

#pragma once

namespace torchlight::platform {

enum class OsKind { kLinux, kWindows, kMacOS, kAndroid };

constexpr OsKind ThisOs() {
#if defined(_WIN32)
  return OsKind::kWindows;
#elif defined(__ANDROID__)
  return OsKind::kAndroid;
#elif defined(__APPLE__)
  return OsKind::kMacOS;
#else
  return OsKind::kLinux;
#endif
}

}  // namespace torchlight::platform
