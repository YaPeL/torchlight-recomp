# Android arm64-v8a through the NDK's own toolchain file, for the android-arm64 presets (this
# project's and the SDK's, tools/deps/android/). minSdk 30 (Android 11, the Galaxy Tab S5e's last
# version; docs/android-port.md). The NDK is r30 (30.0.16248370, the LTS), unpacked under
# ~/android-sdk/ndk/ (docs/android-port.md, stage A0); another one: cache variable
# TORCHLIGHT_ANDROID_NDK.
set(TORCHLIGHT_ANDROID_NDK "$ENV{HOME}/android-sdk/ndk/30.0.16248370" CACHE PATH
    "The Android NDK the android-arm64 presets build with")
if(NOT EXISTS "${TORCHLIGHT_ANDROID_NDK}/build/cmake/android.toolchain.cmake")
  message(FATAL_ERROR "No Android NDK at ${TORCHLIGHT_ANDROID_NDK} (TORCHLIGHT_ANDROID_NDK)")
endif()

set(ANDROID_ABI arm64-v8a CACHE STRING "")
set(ANDROID_PLATFORM android-30 CACHE STRING "")
# One libc++ for the SDK, OGRE and the game, shared: they are separate libraries in one process.
set(ANDROID_STL c++_shared CACHE STRING "")

include("${TORCHLIGHT_ANDROID_NDK}/build/cmake/android.toolchain.cmake")
