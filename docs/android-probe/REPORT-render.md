# Android build probe: report for the render agent

Branch `android-probe` (from `feature/android-build` 70d3fc4); never integrated. The local fixes are
the `.diff` files next to this file. None of them is committed anywhere else.

NDK r30 (30.0.16248370, clang 21.0.0), arm64-v8a, API 30, `c++_shared`; CMake 4.4.4, Ninja; M2 host.

## OGRE 14.6 with GLES2

- **It builds for Android** in 26 s, from `tools/deps/build_ogre.sh` with the `android-arm64`
  target (70d3fc4).
- **The fix is in the script, not in OGRE.** OGRE tests `APPLE` before `project()`, so on a macOS
  host it enabled OBJC with the NDK clang. OGRE skips OBJC when `ANDROID_PLATFORM` is set, so the
  script now passes `-DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-30` up front.
- Options: GL3Plus OFF, GLES2 ON, the Android JNI sample OFF.

## OGRE is only static on Android

- OGRE builds only static libraries there: `libOgreMainStatic.a`, `libRenderSystem_GLES2Static.a`,
  `Codec_STBI` static, and the rest.
- So the render system can't be loaded as a plugin from disk; `src/backend` needs static plugin
  registration on Android. Your call.
- Today `src/backend/CMakeLists.txt:51` looks for `RenderSystem_GL3Plus.so` and `Codec_STBI.so`
  files, and warns that neither exists.

## OGRE's install leaves out the NDK's cpufeatures

- `OgreMain` links it as `$<BUILD_INTERFACE:cpufeatures>`, so consumers fail with undefined
  `android_getCpuFeatures` and `android_getCpuFamily`.
- Locally: `${ANDROID_NDK}/sources/android/cpufeatures/cpu-features.c` built as a static library
  and linked with `torchlight_backend` (`game-4-ogre-cpufeatures.diff`).

## No GLES precedent

Rayman (XDanfr/RaymanOriginsRecomp) renders only with Vulkan: xenos, or their own Vulkan renderer.
edeegg's APK uses xenos. There is nothing to borrow for GLES2.

## How far the game gets

- **`libtorchlight.so` links.** That takes the SDK built with Rayman's 0001 (`REPORT-rexglue.md`)
  and the local fixes listed below; the whole game compiles.
  - 208 MB with debug info, 55 MB stripped; `librexruntime.so` is 9.4 MB stripped.
  - Its LOAD segments are 16 KB aligned.
  - `SDL_main` is exported.
  - NEEDED: `libz`, `librexruntime`, `libm`, `libOpenSLES`, `libandroid`, `libdl`, `liblog`,
    `libc++_shared`, `libc`.
- **The first error** is codegen running the Android `rexglue` binary on the host. It belongs to the
  SDK (`REPORT-rexglue.md`); the local fix is `game-1-codegen-host-tool.diff`.

The other errors, in order, each fixed only locally to go on:

| # | Error | Local fix |
|---|---|---|
| 2 | `src/platform/platform_linux.cpp:27`: `'X11/Xlib.h' file not found`. The game's `UNIX AND NOT APPLE` treats Android as Linux (`src/platform/CMakeLists.txt`, `src/achievements/CMakeLists.txt`) | `game-2-platform-without-x11.diff`: the X11 parts cut out on Android. A stand-in, not an Android platform |
| 3 | `achievement_hooks_test`: `-msse4.1` unsupported for aarch64. It sits under `UNIX AND NOT APPLE` with no processor guard, unlike `torchlight_frustum_test` | `game-3-achievements-test-x86-only.diff` |
| 4 | undefined `android_getCpuFeatures` and `android_getCpuFamily` (OGRE, above) | `game-4-ogre-cpufeatures.diff` |
| 5 | `undefined symbol: main`. On Android the game has to be a shared library that SDLActivity loads, not an executable | `game-5-shared-library.diff`: `add_library(torchlight SHARED)` |

The configure fixes:

- The game needs `rexglue_DIR`, `OGRE_DIR` and `CMAKE_FIND_ROOT_PATH` set to the two installs
  (`preset-game-find-sdk-and-ogre.diff`).
- The NDK's toolchain searches packages only under its roots, so `rexglueConfig.cmake` could not
  find its own dependencies (fmt, spdlog, SDL3…) without them.

**Not done:**

- no registration of the GLES2 render system or of STBI;
- no Android platform implementation;
- nothing ran on a device.

## Run-time DXT textures (your measurement)

- For `docs/android-port`, merge only up to d86b5ad.
- a0de0f0 holds the earlier "measure later, maybe ETC2 on another thread" note, which your result
  replaces: 26 run-time textures per run, all A8R8G8B8, about 9 MiB, none DXT.
- Only file textures go through the ETC2 cache, prepared ahead of time.

## Build times (M2)

| Part | Time |
|---|---|
| SDK configure | 20 s (4:15 the first time, mostly the submodule clone) |
| SDK build | 1:39 |
| OGRE | 26 s |
| Game, full build | 8:41 (generated code) |
| Game, relink | 13 s |

## Benchmark (one core)

- **M2:** integer 286.0 ms, pointer chase 218.8 ms, float 152.1 ms.
- **POCO X7 5G and Galaxy Tab S5e:** pending until USB debugging is on. The results go to
  `BENCHMARK.md` next to this file.
- Binary: `aarch64-linux-android30-clang++ -O2 -march=armv8.2-a`, static libc++, a 16 KB maximum
  page size.
- Kernels:
  - an integer and branch state machine over 4 MiB, ×40;
  - a pointer chase over 16 MiB, 20M steps;
  - a 64-body n-body in float, 40000 steps.

  Each kernel reports its best of 5 runs.
