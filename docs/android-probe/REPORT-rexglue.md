# Android build probe: report for the ReXGlue agent

Branch `android-probe` (from `feature/android-build` 70d3fc4); never integrated. Every fix below was
tried in a local working copy only. The diffs are next to this file; none of them is part of
`patches/series`.

## Setup

- SDK base: `bd833a2` + `patches/series` (the non-Windows entries, as `tools/deps/build_sdk.sh`
  applies them).
- NDK r30 (30.0.16248370, the LTS; clang 21.0.0), arm64-v8a, API 30 (minSdk), `c++_shared`.
- CMake 4.4.4, Ninja Multi-Config, macOS (M2) host.
- The SDK preset `android-arm64` (`tools/deps/android/CMakeUserPresets.json`) and the toolchain
  file `cmake/android-arm64.toolchain.cmake`, which wraps the NDK's.

## First error (unchanged SDK)

`CMakeLists.txt:4`: `if(APPLE) set(REXGLUE_LANGUAGES C CXX OBJC OBJCXX)` before `project()`.

- On a macOS host, `APPLE` is still the host's before the toolchain file runs, so CMake tests OBJC
  with the NDK clang.
- ld64.lld then fails: `unknown argument '--build-id=sha1'`, `--no-undefined-version`,
  `--fatal-warnings`, `--no-undefined`.
- The fix is `if(APPLE AND NOT DEFINED ANDROID_ABI)` (Rayman's 0001). It only works when the
  consumer passes `-DANDROID_ABI`/`-DANDROID_PLATFORM` on the command line or in the preset, not
  only in the toolchain file. Our preset change for that is `preset-sdk-android-abi-before-project.diff`.
- OGRE 14 has the same issue; `tools/deps/build_ogre.sh` already passes both up front (70d3fc4).

## Errors and the fix that resolved each

The probe ran on series + the one-line fix above, then added pieces of Rayman's 0001
(`sdk-rayman-0001.diff`) and rebuilt with `-k 0` after each step:

| # | Error | Fix that resolved it (all in Rayman 0001) |
|---|---|---|
| 1 | `CMakeLists.txt:4`: OBJC enabled on a macOS host (above) | `CMakeLists.txt`: `if(APPLE AND NOT DEFINED ANDROID_ABI)` |
| 2 | `src/ui/CMakeLists.txt:151`: `pkg_check_modules(X11_XCB REQUIRED x11-xcb)`; Android takes the `UNIX AND NOT APPLE` branch | `src/ui/CMakeLists.txt`: `elseif(ANDROID)` with `surface_android.cpp` and a link to `android`; new `include/rex/ui/surface_android.h` and `src/ui/surface_android.cpp` |
| 3 | `include/rex/chrono/chrono.h:131/154` and `chrono_steady_cast.h:29/56`: explicit specialization of an undeclared `clock_time_conversion` (the NDK's libc++ has no `clock_cast`) | `include/rex/chrono/chrono.h`: the shim taken for Android as for macOS |
| 4 | `src/ui/window_sdl.cpp:38`: `'X11/Xlib-xcb.h' file not found` | `src/ui/window_sdl.cpp`: an Android branch (ANativeWindow from SDL3) |
| 5 | `src/core/threading_posix.cpp:48`: `'rex/main_android.h' file not found`; `src/core/memory_posix.cpp:94`: `no member named 'GetAndroidApiLevel'`. bd833a2 references a header it does not ship | new `include/rex/main_android.h`; `memory_posix.cpp` includes it |
| 6 | `src/core/fiber_posix.cpp:32/47/55/68`: `getcontext`, `makecontext` and `swapcontext` undeclared (Bionic has none of them) | an AArch64 assembly context switch (`fiber_posix.cpp`, `include/rex/thread/fiber.h`) |
| 7 | Behind those: no robust mutexes in Bionic (`pthread_mutex_consistent`), `std::from_chars` for floats, `std::jthread` and `stop_token`, `/proc/self/exe` (it is `app_process`), `rexcore` linking `pthread rt` | see the list below |

The fixes for row 7:

- `threading_posix.cpp`: `MutexConsistent` is a no-op on Android, and the robust attribute is not
  set.
- `include/rex/string/numeric.h`: `portable_float_from_chars` is used on Android as on macOS.
- `src/core/CMakeLists.txt`: `timer_queue.cpp` gets `-fexperimental-library` for
  `APPLE OR ANDROID`, and Android links `dl log`.
- `filesystem_posix.cpp`: the executable path comes from `dladdr`, and `content://` is stubbed.

With all of 0001 the SDK builds clean: `-k 0`, rc 0, 1:39 (configure 20 s).

## Rayman's patches as they are

- **0001 is enough for the SDK to build.** It applied with git's 3-way fallback and no conflicts.
- **0002** (the AndroidInitialize hooks) applies on top and builds, but is not needed to compile.
  The installed SDK the game linked against has 0001 and 0002 (`sdk-rayman-0002.diff`).
- **0003** (null plugin) and **0004** (no presenter) conflict with our series'
  `rexglue-gpu-null-plugin.patch`.
- **0005** (commit with `mprotect`) applies clean but is not needed to compile.
- **0006** (WaitMultiple on a condition variable) conflicts with `rexglue-posix-wait-fraction.patch`.
- **0007** (packed device) is specific to their game.
- 0005 and 0006 are run-time fixes in their repository: a `/proc/self/maps` walk that stalled
  loading, and a poll that took 73 % of a core. Worth reviewing for us once something runs.

## Game-side findings that belong to the SDK

- **Codegen on a cross build.** The codegen step in `generated/rexglue.cmake:120` runs
  `$<TARGET_FILE:rex::rexglue>`, which is the target (Android) binary, so the host fails with
  "cannot execute binary file" (exit 126).
  - Locally, `rex::rexglue` was pointed at the host SDK's tool of the same version
    (0.10.0.24-dev.gbd833a2); see `game-1-codegen-host-tool.diff`.
  - The output was identical: the same `codegen.build.stamp`.
  - A cross build needs a host codegen path, for example an option for the codegen executable.
- **The package registry.** `cmake/rexglue_install.cmake:170` writes
  `~/.cmake/packages/rexglue/<hash>` on every install. An Android install registered there can be
  found by a desktop configure. The entry this probe created was deleted by hand.
- `rexglue_helpers.cmake:60`: `-msse4.1` is correctly guarded by x86_64.

## Shared memory

`shm_open` is not a problem. The SDK's Android branch already maps guest memory through
`ASharedMemory_create` (API 26+, loaded with `dlopen`) or `/dev/ashmem` (`src/core/memory_posix.cpp`).
Rayman's patches don't touch it either. That path has never run on a device in ReXGlue; stage A1
runs the SDK's tests there.

## Proposal: the 0x1000 host offset on Android (not in the series yet)

- **Today.** `rex::memory::detail::PhysicalHostOffset` (`include/rex/system/xmemory.h:41`) is
  `constexpr`: 0x1000 for guest addresses at or above 0xE0000000 on
  `REX_PLATFORM_WIN32 || (REX_PLATFORM_MAC && defined(__aarch64__))`, and 0 elsewhere. The
  recompiled code does the same arithmetic.
- **The problem.** Android compiles as "elsewhere", so it gets 0. But on Android the page size is
  only known at run time: the same APK runs on 4 KB devices (Galaxy Tab S5e, Android 11) and on
  16 KB ones (newer Android 15+ devices, the ps16k emulator image).
- **Why the shift is safe there.** It works for any host granularity of 4 KB or more: Windows uses
  it with 64 KB, macOS arm64 with 16 KB.
- **Proposal.** Build Android always with it, as macOS arm64: add `REX_PLATFORM_ANDROID` (aarch64)
  to that condition and to the mapping code that depends on it.
- **On our side,** `cmake/guest_host_offset.cmake` gets the same line
  (`TORCHLIGHT_GUEST_HOST_OFFSET=0x1000u`). The game probe build above still uses 0, consistent with
  the current SDK.
- **Verification.** The SDK's PPC and unit tests through `adb shell`, on a 4 KB and on a 16 KB
  device or image.
