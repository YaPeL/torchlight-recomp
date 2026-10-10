# Android port (arm64-v8a): survey and plan

Survey only, no code (2026-10-10). Base: `develop` at `924d49d`, after the macOS port (MAC.1–MAC.9,
docs/macos-port.md on `docs/macos-port-plan`), which made the game run on ARM64. Sources are linked
where a claim comes from outside this repository; what could not be checked against a primary source
or on a device is marked *unverified*. No Android device, emulator, SDK or NDK is on the development
Mac today (section 6).

## Summary

| Area | Finding | Blocks a first build? |
|---|---|---|
| ARM64 core | Patches 23–25 (`fctiw`/`fctid`, `mffs`, `mtfsf`), the memory fences of the SDK base and MAC.2's translation carry over from macOS. The 16 KB handling does too, but the SDK picks the host offset at compile time per platform, and an Android APK meets both 4 KB and 16 KB devices | Yes: Android must build with the 16 KB-safe offset (an SDK patch) |
| SDK | ReXGlue keeps Xenia's Android code (`REX_PLATFORM_ANDROID`: `ASharedMemory`, threads), but has no Android preset or toolchain, and it was never built for Android | Yes: a `android-arm64` preset and the NDK toolchain, as a patch |
| Render | OGRE's GLES2 render system (GLES 3.x when available) with RTSS's GLSL ES writer; Vulkan later. The backend uploads DXT1/3/5 as they come from the guest; Mali and Adreno have no BCn | Yes: DXT must be decoded or transcoded in the backend's conventions module |
| Window and lifecycle | SDL3 has Android support (SDLActivity, lifecycle events, `ANativeWindow`); OGRE's Android EGL window takes an `ANativeWindow` and can keep its context when the surface goes | No, but surface loss is the main risk (section 4) |
| Controls | The game is built for a gamepad, which helps: a touch overlay emulates one. The closest precedent, UnleashedRecomp-Android, draws it in-engine and hides it on gamepad input. A setting Auto / Always / Never (A5) | No; gamepad first |
| Game data | Pick the XBLA package with Android's document picker, copy it into app storage, install as today | No |
| Packaging | Signed arm64 APK on GitHub Releases; no Play (single commercial game, user-supplied data; precedent below) | No |
| Performance | CPU-bound on desktop (the guest's own code); a mid-range phone is slower per core: 30 fps is the realistic target, 60 to be measured | Unknown until measured |
| Memory | Never measured on desktop; to measure before any device work | Unknown |
| Devices | A Galaxy Tab S5e and a POCO phone (the owner's); the Apple Silicon emulator with a 16 KB image for bring-up; the tools (9.7 GB) approved | Yes: tools to install (section 6) |

Recommendation: the same order as macOS, each stage validated before the next: SDK on Android with
its tests on a device, then OGRE GLES 3 with the backend's replays, then a skeleton APK with the
lifecycle, then the game to the main menu with a gamepad, then touch controls, then performance,
then the package and CI. GLES 3 first, Vulkan after the first release, as macOS did with GL3+ and
MoltenVK. Section 7 has the stages.

## 1. What other ports do

Projects with an Android build checked (2026-10): UnleashedRecomp-Android (an Xbox 360
recompilation of the XenonRecomp family, the closest match:
https://github.com/SansNope/UnleashedRecomp-Android), Goemon64Recomp-Android and HarvestMoon64Recomp
(N64Recomp: https://github.com/ogdanimal/Goemon64Recomp-Android,
https://github.com/igawa6/HarvestMoon64Recomp), Shipwright-Android and 2ship2harkinian-Android
(https://github.com/Waterdish/Shipwright-Android), and the emulators Dolphin, PPSSPP, RetroArch,
Azahar, Eden. Not found: an Android build of Zelda64Recomp itself or of MarathonRecomp; re3/reVC and
the Perfect Dark port *unverified*.

**Touch controls.**

- UnleashedRecomp-Android: drawn in-engine with ImGui (`ui/touch_controls.cpp`, `ImDrawList`
  images), from SDL finger events. Positions and sizes as fractions of the viewport height, an
  in-game drag-and-resize editor with a reset, the layout saved in a file of its own. The layout
  follows the context (the stick becomes a D-pad in menus, a single skip button in cutscenes).
  Visibility Auto / Always / Off; Auto hides the overlay when a physical controller sends input
  and shows it again on the next touch.
- The N64 forks: a native Android `View` over SDL's surface (Goemon: `TouchOverlayView`, hidden when
  `isGamepadEvent()` sees pad input; HarvestMoon: XML shape drawables, a floating left stick that
  re-centres under the thumb).
- Shipwright and 2ship: touch only in their ImGui menus; gameplay needs a controller.
- Dolphin: a `SurfaceView` of PNG bitmaps, an edit mode, opacity and scale; no automatic hiding
  (asked for by users). PPSSPP: its own C++ UI, per-game layouts for portrait and landscape, a
  floating stick, controls that fade after 8 s without a touch. RetroArch: PNG overlays described
  by `.cfg` files, an option to hide them while a gamepad is connected.

Common pattern: the overlay is drawn by the engine or by a native view; layout as fractions of the
screen; an editor (move, resize, opacity); a floating movement stick. The best hiding rule seen is
Unleashed's: hide on the first physical-pad input, show on the next touch (better than hiding on
connection, which a phone with a connected but unused pad gets wrong, or a timer).

For this port: the backend already has a host UI pass (`tl_backend_set_ui_frame`, used by the game
menu's video settings and the save import), so the overlay can be drawn there, over the game,
without ImGui or a Java view, and the controls feed the same virtual pad the SDK's input driver
reads (`--mnk_mode` already maps keys to a pad). Torchlight XBLA is played with a pad (move on the
left stick, attacks and skills on the face buttons and the triggers, the D-pad for quick use), so a
pad overlay covers the game; no tap-to-move is needed. Its layout and controls are a design task of
their own (stage A5).

**Game data.**

- UnleashedRecomp-Android: the Storage Access Framework (SAF) picker (`ACTION_OPEN_DOCUMENT`,
  `ACTION_OPEN_DOCUMENT_TREE`) for a zip, a folder or packages; the files are copied into
  app-owned storage (`Android/media/<package>/`) and checked; a `DocumentsProvider` shows the app's
  folders to file managers (Android 11+ hides `Android/data`). No storage permission.
- The N64 forks: the ROM picked with SAF and copied into private storage.
- Dolphin: SAF made its game list more than ten times slower; it moved its user folder into
  app-specific storage
  (https://www.xda-developers.com/dolphin-emulator-limited-functionality-android-scoped-storage/).
- Play rejects `MANAGE_EXTERNAL_STORAGE` for "a file selection activity where the user manually
  selects individual files" (https://support.google.com/googleplay/android-developer/answer/10467955).
  OBB expansion files are deprecated.

For this port: one SAF pick of the XBLA package (a single file; SDL3's `SDL_ShowOpenFileDialog`
uses SAF on Android and `SDL_IOFromFile` opens `content://` URIs), copied into app-specific storage,
then `game_setup::Install` as on every platform (it checks the package and copies 200 MB of game
files). The game then reads plain files with POSIX I/O. A folder pick (an already extracted
package) returns a tree URI that plain paths cannot walk: it would need `DocumentsContract`, so the
first version takes the package file only.

**Packaging and distribution.**

- Every recompilation and decompilation port surveyed ships a signed arm64-only APK on GitHub
  Releases; none is on Play. minSdk 28–29, targetSdk 34–36. UnleashedRecomp-Android checks its
  latest GitHub release from the app.
- Eden (a Switch emulator) reached Play on 2025-09-12 and was gone about two weeks later; it
  ships on GitHub and Obtainium now (https://www.androidauthority.com/eden-emulator-3601673/). A
  port of one commercial game that needs the user's files is a takedown risk on Play.
- Play needs an AAB, target API 35 (36 from 2026-08-31, extendable to 2026-11-01:
  https://support.google.com/googleplay/android-developer/answer/11926878) and 16 KB pages
  (section 2).
- Google's developer verification applies to sideloaded installs on certified devices too: enforced
  since 2026-09-30 in Brazil, Indonesia, Singapore and Thailand, worldwide planned for 2027
  (https://f-droid.org/2026/02/24/open-letter-opposing-developer-verification.html). The package
  name and signing key will need registering before that.
- F-Droid needs reproducible builds and has rules on non-free assets: unlikely.

For this port: a signed APK (arm64-v8a, targetSdk 35+, 16 KB aligned) on GitHub Releases with the
other platforms' downloads and in `SHA256SUMS`. The signing key is a new secret of the `release`
environment, generated and kept by the owner (A7); losing it means users cannot update in place.

## 2. NDK, toolchain and what ARM64 work carries over

**NDK.** r28 (2025-02), r29 (2025-10); r30 is the current LTS
(https://developer.android.com/ndk/downloads). Each ships its own clang and libc++, linked
statically by default (https://developer.android.com/ndk/guides/cpp-support); libc++ has
`std::expected` since 16, `std::format` since 17 and `std::print` since 18
(https://libcxx.llvm.org/Status/Cxx23.html), so the project's C++23 should build. The exact LLVM
version of each NDK and `std::print` on a device are *unverified*: the first SDK build answers it.
The NDK's `android.toolchain.cmake` drives CMake for the SDK, OGRE and the game alike.

**Build order**, as on Linux and macOS (`tools/deps/`):

1. The SDK: a new `android-arm64` preset (the NDK toolchain file, `ANDROID_ABI=arm64-v8a`,
   `ANDROID_PLATFORM` 29 or later), as a patch in `patches/`. Its third-party code builds from
   source with CMake (SDL3, FFmpeg through its CMake overlay, fmt, spdlog, SIMDe); the codegen tool
   (`rexglue`) stays a host tool (Linux or macOS), since codegen output does not depend on the
   target.
2. OGRE 14.6: OGRE's own Android path (the NDK toolchain file; GLES2 render system, RTSS, the
   STBI codec), static or shared, without its Java/SWIG samples
   (https://ogrecave.github.io/ogre/api/latest/building-ogre.html). `build_ogre.sh` gets an
   Android branch.
3. The game: `generated/` from the same codegen, compiled for arm64-v8a into `libmain.so`, loaded by
   SDL's `SDLActivity`.

**What carries over from macOS.**

- Patches 23–25 (`rexglue-fctiw-rounding-mode`, `rexglue-arm64-mffs-rounding`,
  `rexglue-mtfsf-field-mask`): the ARM64 floating-point conversions and FPSCR. Same CPU
  architecture, same code paths; the SDK's PPC tests on a device confirm them.
- The SDK base `bd833a2` (sync/lwsync/eieio fences, needed on ARM64's weaker memory order).
- `kHostOffset` and `xbox_memory::HostAddress` (MAC.2): the project reads guest memory through the
  same translation as the SDK.
- 16 KB pages: macOS already runs with 16 KB host pages and 4 KB guest pages (views at 16 KB
  offsets, the 0xE0000000 + 0x1000 shift, a host page reconcile; docs/macos-port.md, section 3).
  Write watches have bugs with 16 KB pages, but the native mode's `null` GPU plugin arms none.
- **What does not carry over as is: the page size is a run-time property on Android.** The SDK's
  shift is a compile-time choice per platform: `rex::memory::detail::PhysicalHostOffset`
  (`include/rex/system/xmemory.h`) is `constexpr`, 0x1000 on Windows and macOS arm64, 0 elsewhere,
  and the recompiled code does the same arithmetic; `cmake/guest_host_offset.cmake` mirrors it for
  `kHostOffset`. Android builds as "elsewhere" (4 KB), but the same APK runs on 4 KB and 16 KB
  devices. Since the shift works for any granularity of 4 KB or more (Windows uses it with 64 KB),
  the proposal is to build Android always with it, as macOS arm64: an SDK patch adding Android to
  that condition and to the mapping code that follows it, and the same line in
  `guest_host_offset.cmake`. Verified by the SDK's tests on both a 4 KB and a 16 KB image (A1).
- What differs: Android has no `shm_open`. The SDK's Android branch already maps guest memory
  through `ASharedMemory_create` (API 26+, loaded with `dlopen`) or `/dev/ashmem`
  (`src/core/memory_posix.cpp`), as Dolphin does for its 14 GiB fastmem arena
  (https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/Common/MemArenaAndroid.cpp). That
  branch was written for Xenia and never run in ReXGlue: the first thing to test.

**16 KB pages on Android.** Apps targeting API 35+ must support 16 KB pages on 64-bit devices; the
developer guide now gives 2027-02-01 for updates
(https://developer.android.com/guide/practices/page-sizes). NDK r28+ aligns arm64 ELF segments to 16
KB by default; the APK needs `zipalign -P 16` (AGP 8.5.1+); `check_elf_alignment.sh` and
`llvm-objdump -p` (LOAD `align 2**14`) check it. Tested on the 16 KB emulator image or on a Pixel 8
or later with "Boot with 16KB page size". Since the SDK already handles 16 KB on macOS, this is a
check, not new work.

**Address space.** A 4 GiB `PROT_NONE` reservation fits: Android's GKI kernels have a 39-bit address
space with 4 KB pages and 47-bit with 16 KB pages (the arm64 Kconfig defaults,
https://android.googlesource.com/kernel/common/+/refs/heads/android15-6.6/arch/arm64/Kconfig), and
AOSP sets `vm.overcommit_memory 1`. A fixed low address is not available (ART and the linker live
there); the SDK's guest base is chosen at run time (the code adds it to every guest address), so
only the reservation's own hint and its fallback need checking on a device.

## 3. Render

**Render system: GLES 3 first.** OGRE's GLES2 render system is the production path on Android and
uses GLES 3.x features when the context offers them
(https://github.com/OGRECave/ogre/blob/master/RenderSystems/GLES2/src/OgreGLES2RenderSystem.cpp).
It binds vertex and fragment programs only (no geometry or compute) and no hardware instancing: the
backend uses neither. OGRE's Vulkan render system runs on Android too, but its notes list two
limits that hit this backend directly: updating a buffer the GPU still uses glitches without
`HBL_DISCARD` or triple buffering, and loading a texture or updating a buffer after the frame's
first draw ("rendering interruption") is a problem
(https://github.com/OGRECave/ogre/blob/master/Docs/13-Notes.md). The guest streams buffers and
textures mid-frame. Vulkan is MAC.10's question as well; one investigation can serve both later.

**RTSS.** It has a `glsles` writer next to `glsl`, `glslang` (Vulkan) and `hlsl`
(https://github.com/OGRECave/ogre/blob/master/Components/RTShaderSystem/src/OgreShaderProgramWriterManager.cpp),
and the FFP stages the backend uses (transform, colour, lighting, texturing, fog, alpha test) are
shared by every writer. The backend's own sub-render states (`GuestPixelFog`, `GuestAlphaTest`,
`GuestLighting`, `GuestTexgen`) and its programs are written in OGRE's unified shader macros
(`OgreUnifiedShader.h`), which already build as GLSL and HLSL (Direct3D 11 on Windows); GLSL ES is
the same family. Precision (`mediump` defaults in fragment shaders) is the expected difference:
replays measure it.

**What changes in the backend** (all through OGRE's API, as now; differences in one place):

- **DXT.** `tl_backend_texture` maps `TL_LINEAR_DXT1/3/5` to `PF_DXT1/3/5`
  (`src/backend/backend.cpp`). Vulkan reports from Android devices show ETC2 and ASTC on 99.9% and
  BC on 37.6%, mostly outside Mali and Adreno's own drivers
  (https://vulkan.gpuinfo.org/listfeaturescore10.php?platform=android). Without
  `RSC_TEXTURE_COMPRESSION_DXT` the backend decodes DXT to RGBA8 at upload (DXT1 is 4 bits per
  texel and DXT3/5 8, RGBA8 32: eight and four times the memory), or transcodes it to ETC2 or ASTC
  (CPU cost per upload, about DXT's memory). A
  capability decision, in the conventions module, chosen by the render system's capabilities, not
  by platform. Decoding first (simple, exact); transcoding if memory says so (section 5).
- **Formats and limits.** Every pixel format the backend creates must exist in GLES 3 (some
  16-bit and float formats have no render-target support there), and the vertex element types
  too. An audit of the `tl_*` formats against the GLES render system's tables, then replays.
- **Window.** A `NativeWindow::kAndroid` (the `ANativeWindow*`), the `externalWindowHandle` and
  `preserveContext` parameters (section 4), in `platform_android.cpp`.
- **The emulated GPU (Xenos)** is not part of the Android port: it needs Vulkan and the write
  watches, and only serves diagnostics, as on macOS.

## 4. Lifecycle and the window with SDL3

**SDL3 on Android** (latest release 3.4.8,
https://github.com/libsdl-org/SDL/blob/main/docs/README-android.md): the app is a `libmain.so`
started by `SDLActivity`. Lifecycle: pausing sends `SDL_EVENT_WILL_ENTER_BACKGROUND` and
`DID_ENTER_BACKGROUND`, resuming `WILL_ENTER_FOREGROUND` and `DID_ENTER_FOREGROUND`, `onTrimMemory`
sends `SDL_EVENT_LOW_MEMORY`, finishing sends `TERMINATING` and `QUIT`
(`src/video/android/SDL_androidevents.c`). SDL recommends handling them in an event filter, as the
app may get no CPU after the event is queued. `SDL_HINT_ANDROID_BLOCK_ON_PAUSE` (default on) blocks
the event loop while paused. `SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER` gives the `ANativeWindow*`,
updated when the surface is created again; gamepads hot-plug through `SDLControllerManager`; touch
comes as finger events; `SDL_HINT_ORIENTATIONS` locks landscape.

**The surface goes away.** On Android the window's surface is destroyed whenever the app goes to the
background (and on some devices on rotation or multi-window changes); `onNativeSurfaceDestroyed`
runs on the Java UI thread. SDL waits for its own GL context's backup only for windows it renders
in. Here OGRE owns EGL (`externalWindowHandle`, a window created without `SDL_WINDOW_OPENGL`), so
the backend must let go of the EGL surface before that callback returns, and make it again when the
surface comes back: OGRE's Android EGL window has `_notifySurfaceDestroyed` and
`_notifySurfaceCreated`, and with `preserveContext=true` keeps the EGL context (textures, buffers,
programs) and only remakes the surface
(https://github.com/OGRECave/ogre/blob/master/RenderSystems/GLSupport/src/EGL/Android/OgreAndroidEGLWindow.cpp).
If a device loses the context anyway, OGRE's `notifyOnContextLost` path rebuilds its resources, but
the backend's mirrors of guest textures and buffers would be gone: they would need the guest's data
again (the guest only re-uploads what it changes). That is the hardest part of the port; the
prototype in stage A3 answers it before the game runs.

This fits the macOS work: `platform::RunOnWindowThread` already moves window work to the window
system's thread (the main thread on macOS); on Android the surface callbacks come on the UI thread
while SDL's main runs on its own, so the same hook serves to hand the backend a "surface gone /
surface back" request and wait for it.

**Pause.** While in the background the guest must not run unthrottled (battery, the low-memory
killer) and must not present. The options: block the presenter (the live mode's producer) and let
the guest's threads block on their next present, or suspend the guest's threads (the SDK creates
them suspended at launch, `runtime.h`; a suspend at run time is to be checked). Audio pauses with
SDL's default. The game saves on its own at its usual points; Android may kill a background app
at any time, so a save only happens if the game made it: the same as closing the window on
desktop. To be designed in stage A3 with the lifecycle prototype.

**Rotation.** Landscape only (`SDL_HINT_ORIENTATIONS` and the manifest): the game's frame is 16:9;
the aspect setting (`aspect = "auto"`) follows the screen as on desktop (phones are 19.5:9 to
21:9: the ultrawide path, docs/ultrawide.md).

## 5. Memory and performance on a mid-range phone

**Performance.** On desktop the game is CPU-bound by the guest's own code: on a Core i7-8750H the
town, walking, spends about 9 ms per frame in the game's code and 81–97 fps overall
(docs/performance-profile.md); on the Apple M2 the main menu shows 300–360 fps guest-side
(docs/macos-port.md, MAC.6). A mid-range phone's big cores (Cortex-A76/A78 class at 2.2–2.4 GHz)
are slower per core than either; the main game thread is mostly one core. Without a measurement, a
ratio of 2–3 times the laptop's frame time puts the town at roughly 30–45 fps: 30 fps is the
realistic target, 60 on high-end phones. *Estimate, to be measured* (stage A6). The backend thread
(4.6 ms per frame on the laptop) runs in parallel. Thermal throttling lowers sustained clocks:
Android's thermal API (`getThermalHeadroom`, NDK from API 31,
https://developer.android.com/games/optimize/adpf/thermal) can drive the internal resolution and
the frame cap, which the backend already has (`render_resolution`, `fps_cap`).

**Memory.** Never measured on desktop. What is known: the guest's address space is reserved, not
committed (only touched pages count); the Xbox 360 had 512 MB in total; the backend mirrors guest
textures and buffers as OGRE resources, and DXT decoded to RGBA8 costs four to eight times its
size. Phones have 4–8 GB with no fixed per-app budget; the low-memory killer works by priority and
a background app goes first (https://developer.android.com/games/optimize/vitals/lmk). First step,
before any device work: measure the game's resident and GPU memory on this Mac in town and in a
dungeon (Activity Monitor's footprint, or `footprint`), and the texture memory with DXT decoded.

## 6. Devices and emulators to test on

Nothing for Android is installed on this Mac (no SDK, NDK, `adb` or emulator); the disk has about
22 GB free (2026-10-10).

**What the tools take** (installed sizes, summed from each package's zip directory in Google's
repository, 2026-10-10):

| Package | Download | Installed |
|---|---|---|
| `cmdline-tools;latest` | 0.16 GB | 0.18 GB |
| `platform-tools` (`adb`) | 0.02 GB | 0.04 GB |
| `platforms;android-36` | 0.07 GB | 0.11 GB |
| `build-tools;36.1.0` (`apksigner`, `zipalign`) | 0.08 GB | 0.20 GB |
| `ndk;29.0.14206865` | 1.05 GB | 3.29 GB |
| `emulator` (macOS arm64) | 0.42 GB | 1.32 GB |
| `system-images;android-36;google_apis_ps16k;arm64-v8a` | 1.88 GB | 4.56 GB |
| **Total** | 3.68 GB | **9.70 GB** |

On top of that: one virtual device (its data partition and snapshots grow with use; created with
snapshots off and a 4 GB data partition, it stays near 2-3 GB), the downloads while they unpack
(up to 1.9 GB, freed afterwards), and the Android build trees of the SDK, OGRE and later the game
(a few GB, *estimate*). About 13 GB in all, which leaves about 9 GB of the 22. What is left out to
fit: the 4 KB system image (another 4.6 GB), since the physical devices below have 4 KB pages, so
the emulator only needs the 16 KB one; Homebrew's `android-commandlinetools` (the same tools under
`/opt/homebrew`), installing everything with `sdkmanager` into one folder (`~/android-sdk`) that
can be deleted whole.

**Emulator on the M2**: the Android emulator runs arm64 system images natively on Apple Silicon,
with GLES and Vulkan translated to the host's GL/Metal. Good for bring-up, the SDK's tests on 16 KB
pages, lifecycle and the package; not for performance or GPU behaviour (its GPU is a translation,
and the surface/context behaviour may differ from real drivers).

**Physical devices** (the owner's, 2026-10-10):

- **Samsung Galaxy Tab S5e**: Snapdragon 670 (2 Cortex-A75 at 2.0 GHz, 6 Cortex-A55), Adreno 615
  (GLES 3.2, Vulkan 1.1), 4 or 6 GB, 2560x1600; its last update is Android 11 (API 30), with 4 KB
  pages (*from its specifications; to confirm on the device with `adb shell getprop` and
  `getconf PAGESIZE`*). Below the mid-range target in CPU, and on an older Android: it sets the
  minimum API if it is to be supported (minSdk 30 or lower), and it is the low end the frame-rate
  levers are tried on. Adreno: no DXT, ETC2 and ASTC present.
- **A POCO phone**: model to be named by the owner; it decides the mid-range measurements (A6).

A Bluetooth pad for stages A4-A5 (any Xbox or PlayStation pad), and the Auto mode of the touch
controls is tested with it connected at start and connected later.

## 7. Plan by stages

| Stage | Tickets | Needs | Validated by | Risks |
|---|---|---|---|---|
| A0. Tools | AND.0 | Approved 2026-10-10: command-line tools, NDK, one arm64 emulator image (section 6) | `adb devices` lists the tablet and the phone; the emulator boots the 16 KB image | Disk (about 9 GB left after) |
| A1. SDK on Android | AND.1 | NDK | The SDK builds with an `android-arm64` preset and the 0x1000 host offset (patches); its PPC tests (`ppc_tests`) and unit tests pass through `adb shell` on the 16 KB emulator image and on the 4 KB tablet and phone | `ASharedMemory` path never run in ReXGlue; the offset on 4 KB devices |
| A2. OGRE GLES 3 and the backend | AND.2 | A1's toolchain | OGRE 14.6 GLES2/GLES3 builds; the backend's tests and the 20 captures replayed on the phone against Linux (PSNR); DXT decoded by capability | Precision (`mediump`), GLES format gaps, DXT memory |
| A3. Skeleton APK and lifecycle | AND.3 | A2 | An APK (SDLActivity, `libmain.so`) that replays a capture in a loop and survives background/foreground, screen off/on and low memory without losing the picture | Surface loss with an OGRE-owned EGL; a lost context |
| A4. Game to the main menu | AND.4 | A1–A3; the XBLA package on the phone | First start: package picked with SAF, installed; the main menu with a gamepad; saves on app storage | Memory; file paths and case |
| A5. Touch controls | AND.5 | A4 | An overlay drawn by the backend's UI pass, a layout editor, the Auto / Always / Never setting (below); a dungeon played by touch | Design for a pad-built ARPG on a phone screen |
| A6. Play and measure | AND.6 | A4 | Town and dungeon frame times, memory, thermals on the mid-range phone; 30 fps held | CPU too slow; throttling |
| A7. Package and CI | AND.7 | A1–A6; the owner's signing key (below) | A signed, 16 KB-aligned APK (`check_elf_alignment.sh`, `apksigner verify`); CI builds the deps and the tests without the game; the release job builds and signs the APK at a freeze and publishes it on GitHub Releases only | Developer verification (2027) |

**A5, the touch controls setting** (owner's decision, 2026-10-10). A setting with three values,
in the settings file and, once it exists, in the launcher:

- **Auto** (the default): if a pad is connected at start, the built-in pad of an Android handheld
  included, the touch controls are not shown; the first touch on the screen shows them, and the
  first input from a pad hides them again. With no pad at start they are shown.
- **Always**: shown, whatever pads are connected.
- **Never**: never shown, for the Android handhelds with a built-in pad (Retroid, AYN Odin,
  Anbernic and the like), where a touch on the screen must not bring them up.

A pad is what SDL reports as a gamepad (`SDL_EVENT_GAMEPAD_ADDED`, its button and axis events); the
built-in pad of a handheld reports as one. Validated on the phone with a Bluetooth pad connected
before start and after start, and with none.

**A7, the signing key** (owner's decision, 2026-10-10). The owner generates and keeps the key; the
agents never generate, see or copy it. The CI uses it only as secrets of the `release`
environment. Generated once, on the owner's machine, with the JDK's `keytool` (the Android build
tools need a JDK anyway):

```
keytool -genkeypair -v -storetype PKCS12 -keystore torchlight-release.p12 \
  -alias torchlight -keyalg RSA -keysize 4096 -validity 10000 \
  -dname "CN=Torchlight Recomp"
```

`keytool` asks for the keystore password (with PKCS12 the key's password is the same). Validity
10000 days (27 years): Google asks for at least 25. Then
`keytool -list -v -keystore torchlight-release.p12` prints the certificate's SHA-256 fingerprint.

What to keep:

- The keystore file `torchlight-release.p12` and its password, in at least two places offline (a
  password manager that holds files, and an encrypted copy on another device). Lost, no update can
  install over a version already installed: users would have to uninstall, and their saves live
  in the app's storage (A4). Leaked, anyone can publish an APK that installs over ours.
- The alias (`torchlight`) and the certificate's SHA-256 fingerprint, which is not secret: the
  README publishes it so users can check a download (`apksigner verify --print-certs`), and
  Android's developer verification (2027) registers the package name with it.

The secrets of the `release` environment (Settings, Environments, `release`), set by the owner:
`ANDROID_KEYSTORE_BASE64` (the file, `base64 -i torchlight-release.p12`),
`ANDROID_KEYSTORE_PASSWORD` and `ANDROID_KEY_ALIAS`. The release job writes the file to the
runner's temporary folder, signs with `apksigner` (schemes v2 and v3), and deletes it; no other
job and no pull request sees them.

Order and reasons: A1 first because nothing runs without the SDK, and its tests run without the
game; A2 before any app because replays isolate the renderer, as MAC.5 did; A3 before the game
because the surface and context loss is the main architectural risk and a capture replay is
enough to find it; A4–A6 with the game; A7 last. Vulkan (with MAC.10) after the first Android
release.

**Risks, most serious first.**

1. **Surface and context loss** (section 4): OGRE owns EGL while SDL owns the activity; a device
   that drops the context would leave the backend without its mirrors of guest resources.
   Prototype in A3.
2. **CPU performance**: the game is CPU-bound on desktop; a phone may not hold 30 fps in busy
   dungeons. Measured in A6; the levers are the internal resolution, the frame cap and the
   guest-side work already profiled.
3. **Memory**: unmeasured; DXT decoding multiplies texture memory. Measured on the Mac before A2.
4. **The SDK's Android memory code** (`ASharedMemory`) never run in ReXGlue, and the host offset
   chosen at compile time for a page size known only at run time (A1).
5. **Distribution**: no Play; developer verification applies to sideloading in 2027; the signing
   key is a long-lived secret.
6. **Controls**: a pad overlay on a phone screen for a game designed for a pad; the layout needs
   play-testing (A5).

**Owner decisions** (2026-10-10). The Android tools installed here (section 6, measured first);
the touch controls setting Auto / Always / Never (A5); the signing key generated and kept by the
owner, used by CI as a `release` environment secret (A7); GitHub Releases only, no Play; test
devices: a Samsung Galaxy Tab S5e and a POCO phone (model to be named). Proposed to the ReXGlue
agent: the 0x1000 host offset on Android as a patch of the series (section 2).

## Sources

Other ports: https://github.com/SansNope/UnleashedRecomp-Android,
https://github.com/ogdanimal/Goemon64Recomp-Android, https://github.com/igawa6/HarvestMoon64Recomp,
https://github.com/Waterdish/Shipwright-Android, https://www.ppsspp.org/docs/settings/controls/,
https://docs.libretro.com/development/retroarch/input/overlay/.
Android: https://developer.android.com/guide/practices/page-sizes,
https://developer.android.com/ndk/downloads, https://developer.android.com/ndk/guides/cpp-support,
https://developer.android.com/games/optimize/vitals/lmk,
https://developer.android.com/games/optimize/adpf/thermal,
https://support.google.com/googleplay/android-developer/answer/11926878,
https://support.google.com/googleplay/android-developer/answer/10467955.
OGRE: https://ogrecave.github.io/ogre/api/latest/building-ogre.html,
https://github.com/OGRECave/ogre/blob/master/Docs/13-Notes.md,
https://github.com/OGRECave/ogre/blob/master/Docs/14-Notes.md.
SDL: https://github.com/libsdl-org/SDL/blob/main/docs/README-android.md,
https://wiki.libsdl.org/SDL3/SDL_HINT_ANDROID_BLOCK_ON_PAUSE.
GPUs: https://vulkan.gpuinfo.org/listfeaturescore10.php?platform=android.
