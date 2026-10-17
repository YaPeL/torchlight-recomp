# Android port (arm64-v8a): survey and plan

Survey only, no code (2026-10-10), with the measurements made since (2026-10-17, marked as such).
Two documents merged here: the Mac agent's survey and plan, and the render agent's notes and the
run-time texture measurement; where they disagreed, the owner's decision of 2026-10-17 is
recorded (marked **Decided**). Base of the survey: `develop` at `924d49d`, after the macOS port (MAC.1–MAC.9,
docs/macos-port.md on `docs/macos-port-plan`), which made the game run on ARM64. Sources are linked
where a claim comes from outside this repository; what could not be checked against a primary source
or on a device is marked *unverified*. No Android device, emulator, SDK or NDK is on the development
Mac today (section 6).

## Summary

| Area | Finding | Blocks a first build? |
|---|---|---|
| ARM64 core | Patches 23–25 (`fctiw`/`fctid`, `mffs`, `mtfsf`), the memory fences of the SDK base and MAC.2's translation carry over from macOS. The 16 KB handling does too, but the SDK picks the host offset at compile time per platform, and an Android APK meets both 4 KB and 16 KB devices | Yes: Android must build with the 16 KB-safe offset (an SDK patch) |
| SDK | ReXGlue keeps Xenia's Android code (`REX_PLATFORM_ANDROID`: `ASharedMemory`, threads), but has no Android preset or toolchain, and it was never built for Android | Yes: a `android-arm64` preset and the NDK toolchain, as a patch |
| Render | OGRE's GLES2 render system (GLES 3.x when available) with RTSS's GLSL ES writer; Vulkan later. The backend uploads DXT1/3/5 as they come from the guest; Mali and Adreno have no BCn | Yes: textures converted ahead of time, never during play (section 3) |
| Window and lifecycle | SDL3 has Android support (SDLActivity, lifecycle events, `ANativeWindow`); OGRE's Android EGL window takes an `ANativeWindow` and can keep its context when the surface goes | No, but surface loss is the main risk (section 4) |
| Controls | The game is built for a gamepad, which helps: a touch overlay emulates one. The closest precedent, UnleashedRecomp-Android, draws it in-engine and hides it on gamepad input. A setting Auto / Always / Never (A4) | No; gamepad first |
| Game data | Pick the XBLA package with Android's document picker, copy it into app storage, install as today | No |
| Packaging | Signed arm64 APK on GitHub Releases; no Play (single commercial game, user-supplied data; precedent below) | No |
| Performance | CPU-bound on desktop (the guest's own code); a mid-range phone is slower per core: 30 fps is the realistic target, 60 to be measured | Unknown until measured |
| Memory | Desktop only: about 1 GB without mods on Linux (guest about 330 MB; docs/mods.md 7k); never on a phone | Unknown on a phone |
| Devices | A POCO X7 5G (main) and a Galaxy Tab S5e (floor: Android 11, 4 GB), the owner's; the Apple Silicon emulator with a 16 KB image for bring-up; the tools (9.7 GB) approved | Yes: tools to install (section 6) |

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
their own (stage A4).

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

For this port: a signed APK (arm64-v8a, minSdk 30, targetSdk 35+, 16 KB aligned) on GitHub Releases with the
other platforms' downloads and in `SHA256SUMS`. The signing key is a new secret of the `release`
environment, generated and kept by the owner (A6); losing it means users cannot update in place.

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
  `guest_host_offset.cmake`. Verified by the SDK's tests on both a 4 KB and a 16 KB image (A0).
- What differs: Android has no `shm_open`. The SDK's Android branch already maps guest memory
  through `ASharedMemory_create` (API 26+, loaded with `dlopen`) or `/dev/ashmem`
  (`src/core/memory_posix.cpp`), as Dolphin does for its 14 GiB fastmem arena
  (https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/Common/MemArenaAndroid.cpp). That
  branch was written for Xenia and never run in ReXGlue: the first thing to test.
  A ReXGlue game does publish Android APKs (`edeegg/origins-recomp`, Rayman Origins; noted in
  docs/installer-research.md): its Android changes to the SDK were read for A0 (2026-10-17: the
  ReXGlue agent's patch 34 adapts XDanfr/RaymanOriginsRecomp's 0001 and the AndroidInitialize calls
  of its 0002).

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
  (CPU cost per upload, about DXT's memory). Which one is a capability decision, in the
  conventions module, chosen by the render system's capabilities, not by platform.

  **Owner's decision (2026-10-12): textures are never converted during play.** No decode or
  transcode at upload while the game runs (it would cost frame time on the guest's loads, worst
  on the floor device). Instead:

  - a desktop tool converts ahead of time the game's `pak`, mods and texture replacements to what
    the phones read (ETC2 or ASTC, or RGBA8 where those lose too much), and its output is copied
    to the phone;
  - on the phone, conversion runs only when something is imported or turned on (the game package
    at first install, a mod, a replacement pack), from the launcher, with its progress shown, and
    never in the game.

  **Adjusted (owner, 2026-10-16), after the render agent's proposal.** The render agent found DXT
  textures the game creates at run time, which cannot be prepared ahead of time. So:

  - textures that come from files (the `pak`, mods, `replacements`) are converted to ETC2 at
    install or on the first start, into a disk cache indexed by the hash of the original file;
    all of it in the backend, by what the render system supports (not by platform);
  - textures born at run time are decoded to RGBA8 on the fly, with no ETC2 encoding during play;
  - the render agent's "convert on first use and show RGBA meanwhile" does not apply: with the
    owner's rule nothing from files is converted during play, so the cache is filled before the
    game starts (install, first start), never lazily;
  - how many run-time DXT textures there are and how much memory RGBA8 adds is measured by the
    render agent on desktop, after the rc1, with a temporary counter, in one game with EE and one
    without mods. If they turn out to be many or large, encoding them to ETC2 on another thread,
    with a cache by content hash, is evaluated.

  The desktop tool above stays an option for packs prepared ahead of time; the backend's cache is
  what makes the phone not depend on it.

  **Measured (render agent, 2026-10-17): the textures the game creates at run time.** A temporary
  counter in the frontend (local branch, not in the repository) recorded every texture taken
  through the dynamic path (no file: content captured from the guest's memory), with its format
  after detiling, size, mip count and every upload. Linux, `develop` after v0.12.5-beta, two
  automatic runs on copies of the fixed-floor saved game, one without mods and one with Enhanced
  Edition and its two companion mods: mine floor 1 with a fight (god mode), the inventory and its
  tabs, the Y, START and LT menus, five floors down with a fight on each, six up to the town (18
  level loads, about 3 minutes each).

  | | No mods | Enhanced Edition |
  |---|---|---|
  | Textures created at run time | 26 | 26 |
  | Of them DXT | **0** | **0** |
  | Size in all (A8R8G8B8) | 8.9 MiB | 8.7 MiB |
  | Uploads in all | 41 | 26 |
  | Uploaded more than once | 1 (128 x 128, 16 times within 0.7 s, once) | 0 |

  - Every one is uncompressed A8R8G8B8: the five CEGUI glyph sheets (one of 512 x 512, four of
    256 x 256, 2 MiB, at start and at a font's first use), and one to three per level load
    (512 x 256, 512 x 128, 256 x 256 or 128 x 128, with mips), made during the load and not
    touched afterwards. Nothing is regenerated on a rhythm.
  - So no run-time texture needs DXT handling: they are already RGBA (about 9 MiB) and the
    "decode run-time DXT to RGBA" rule above has nothing to act on in these routes; the DXT work is
    the files' textures, into the cache before play.
  - **The full map and a large fight (2026-10-17, the same tool, no mods and Enhanced Edition):**
    the full map overlay (RS) open for about 16 s while walking, then four champions and six
    skeletons summoned next to the character (`CHAMPION SKELETON`, `UNIT SKELETON`) and fought for
    about 40 s, then a floor down with the map again and three more champions. 10 and 9 textures
    created at run time, **none DXT**, all A8R8G8B8 (3.5 and 3.0 MiB), each uploaded once. No
    capture was taken in these two runs; the summon commands' return is the one the mods agent saw
    when the monsters appeared in its runs. **Closed:** the game creates no DXT texture at run time
    in the screens and fights measured.

  **Decided (owner, 2026-10-17): the run-time rule stays, as a safety net.** If a DXT texture is
  ever created during play on a device without S3TC, it is decoded to RGBA and a line goes to the
  log. Today none appears (measured with and without Enhanced Edition, the full map and a large
  fight included). The adjustment of 2026-10-16 had started from "the render agent found DXT
  textures the game creates at run time": that came from the API (`tl_backend_texture_linear`
  accepts DXT from the guest's memory), not from a measurement.

  **Superseded:** the render agent's first proposal (convert a file's texture on its first use,
  RGBA meanwhile) is replaced by the owner's rule above: the cache is filled before the game
  starts, never during play.

  **ETC2 or ASTC for the cache (evaluated 2026-10-17, for A2; no code yet).** The POCO X7 5G,
  measured by the owner through `adb`, has no DXT in GLES or Vulkan (`textureCompressionBC` 0), so
  the cache of converted textures is required; it has ETC2 and ASTC (LDR and HDR).
  - **Measured on the game's `pak`:**
    - 2836 DDS files: 2040 DXT1 (43.6 Mpx at the top level), 779 DXT3 (25.8 Mpx), 16 DXT5
      (0.4 Mpx). About 93 Mpx with the mips.
    - A seeded sample of 80 of them (128 px or more, no cube maps, 2.76 Mpx; 41 opaque, 39 with
      alpha), decoded from DXT and re-encoded. PSNR is measured against the DXT decode, which is
      what the guest shows today. Times are encode time on one core of the Linux machine
      (i7-8750H at 2.4 GHz, AVX2), astcenc 46c2caa and etcpak 3d716e1.

    | Format (bits per texel) | Opaque, RGB PSNR (worst 10 %) | With alpha, RGB / A PSNR | Speed, Mpx/s per core |
    |---|---|---|---|
    | ETC2 RGB / RGBA (4 / 8), etcpak | 37.0 dB (34.0) | 36.3 / 49.0 dB | 151 |
    | ASTC 4x4 (8), `-fast` | 58.8 dB (56.8) | 50.4 / 61.0 dB | 3.0 |
    | ASTC 4x4 (8), `-medium` | 59.2 dB (57.4) | 52.9 / 62.9 dB | 1.6 |
    | ASTC 5x5 (5.12), `-medium` | 43.7 dB (40.2) | 40.5 / 54.0 dB | about 0.2 |
    | ASTC 6x6 (3.56), `-fast` | 39.2 dB (35.8) | 36.3 / 46.1 dB | 2.5 |
    | ASTC 6x6 (3.56), `-medium` | 40.2 dB (36.4) | 37.2 / 44.6 dB | 0.8 |

    (5x5's speed comes from per-file wall times, the others from the encoders' own timers.)
  - **Quality.** ASTC 4x4 holds a DXT block almost exactly: a DXT1 or DXT3/5 block is two
    endpoints and 2-bit weights on 4x4 texels, which one ASTC 4x4 partition with 2-bit weights
    can carry. So it is nearly lossless against what the game shows today (about 59 dB opaque, 51-53
    dB with alpha), where ETC2 loses visibly in places (37 dB, 34 dB for the worst tenth). Even
    ASTC 6x6, at fewer bits than ETC2, beats it.
  - **Memory.** DXT3/5 are 8 bits per texel, as are ETC2 RGBA and ASTC 4x4: no change. DXT1 is 4,
    as is ETC2 RGB; ASTC 4x4 doubles it. For the whole `pak`, if every level were resident (an upper
    bound; the game keeps far fewer at once), that is about 29 MB more than ETC2 or DXT: about
    64 MB for DXT/ETC2 against 93 MB for ASTC 4x4. ASTC 6x6 for DXT1 would be about 26 MB instead
    of 29, at 40 dB.
  - **Conversion time.**
    - etcpak's ETC2 is about 50 times faster than ASTC 4x4 `-fast`: the `pak` takes about 0.6 s
      on one desktop core, against about 31 s (2 min at `-medium`).
    - On the phones, per core, *estimate*: about 3-4 times slower than this machine, with NEON
      instead of AVX2. They have 8 cores, though, and the cache is filled once per install, with
      the progress shown.
    - The `pak` at ASTC 4x4 `-fast`: under a minute on the POCO X7 (4 Cortex-A78) and about one
      minute on the Tab S5e (2 Cortex-A75 and 6 Cortex-A55); at ETC2, seconds on both.
    - Texture replacement packs are much larger (TNNR's images are up to 8 times per side). At
      ASTC they would take minutes to tens of minutes on the Tab S5e, so a pack's conversion is the
      long one.
  - **Support on the Tab S5e (Adreno 615, GLES 3.2).**
    - ASTC LDR is core in OpenGL ES 3.2 (`GL_KHR_texture_compression_astc_ldr` promoted), so a
      GLES 3.2 GPU has it.
    - OGRE's GLES2 render system sets `RSC_TEXTURE_COMPRESSION_ASTC` when the driver lists that
      extension (`OgreGLES2RenderSystem.cpp`), and ETC2 on any GLES 3.0 context.
    - HDR is not needed: the game's textures are LDR. Whether Adreno 615 lists HDR does not matter.
    - *To confirm on the tablet:* `adb shell dumpsys SurfaceFlinger | grep -o
      GL_KHR_texture_compression_astc_ldr`.
  - **Recommendation for A2:**
    - **ASTC 4x4 `-fast`** for every texture from a file, chosen by capability: ASTC when the
      render system has `RSC_TEXTURE_COMPRESSION_ASTC`, else ETC2, else RGBA8. Both target devices
      take ASTC.
    - It shows what the game shows today, at the same memory for DXT3/5 and twice for DXT1 (a few
      tens of MB at most). Its conversion time is paid once per install, with the progress window.
    - ETC2 stays as the fallback, and as the faster choice if a replacement pack's conversion turns
      out too long on the Tab S5e. That is measured on the device in A2, before choosing per pack.
    - ASTC 6x6 is not worth it: it saves 3 MB over ETC2 for DXT1, and loses 19 dB to ASTC 4x4.
    - The encoder (astcenc, Apache-2.0; etcpak, BSD-3-Clause) goes in with A2, through
      THIRD_PARTY_NOTICES.md.
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
comes as finger events; landscape is kept by the manifest and our activity (below).

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
prototype in stage A1 answers it before the game runs.

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
desktop. Stage A1 (section 7, A1.3) has the design.

**Rotation.** Landscape only, either side, following the system's rotation lock (`userLandscape` in
the manifest, the owner's choice of 2026-10-17; TorchlightActivity keeps SDL from turning the
activity to each window's shape): the game's frame is 16:9; the aspect setting (`aspect = "auto"`)
follows the screen as on desktop (phones are 19.5:9 to 21:9: the ultrawide path, docs/ultrawide.md).

## 5. Memory and performance on a mid-range phone

**Performance.** On desktop the game is CPU-bound by the guest's own code: on a Core i7-8750H the
town, walking, spends about 9 ms per frame in the game's code and 81–97 fps overall
(docs/performance-profile.md); on the Apple M2 the main menu shows 300–360 fps guest-side
(docs/macos-port.md, MAC.6). A mid-range phone's big cores (Cortex-A76/A78 class at 2.2–2.4 GHz)
are slower per core than either; the main game thread is mostly one core. Without a measurement, a
ratio of 2–3 times the laptop's frame time puts the town at roughly 30–45 fps: 30 fps is the
realistic target, 60 on high-end phones. *Estimate, to be measured* (stage A5).

**Decided (owner, 2026-10-17): A5's targets.** On the Galaxy Tab S5e (the floor) 30 fps held, with
no drops in fights; on the POCO X7 5G (the main device) 60 fps.

The two estimates behind it: the render agent's for the Tab S5e alone (2026-10-16) was higher: its two fast cores (Cortex-A75-class at 2.0 GHz) at a rough 4-5 times the
M2's time per core, about 12-15 ms of game thread per frame, 60 in menus and possibly below it in
fights. The two estimates start from different desktop baselines (the i7's frame time against the
M2's); neither is measured. A Raspberry
Pi 5 (Linux arm64, Cortex-A76 at 2.4 GHz) could measure the game thread before any Android work,
with the clock lowered to approach the phones (its GPU lacks OpenGL 3.3, so software GL there). The backend thread
(4.6 ms per frame on the laptop) runs in parallel. Thermal throttling lowers sustained clocks:
Android's thermal API (`getThermalHeadroom`, NDK from API 31,
https://developer.android.com/games/optimize/adpf/thermal) can drive the internal resolution and
the frame cap, which the backend already has (`render_resolution`, `fps_cap`).

**Memory.** Measured on desktop only (render agent, docs/mods.md 7k, Linux, mine's first floor):
guest resident 331 MB, the rest of the process 682 MB, GPU 112 MB without mods; with TNNR in
`replacements` 477 / 999 / 279 MB before v0.12.5-beta's fix, 351 MB guest after it. On Windows
(v0.12.5-beta's check) the guest stayed at 303-384 MB over four level loads with TNNR. Not measured
on macOS or a phone. What is known besides: the guest's address space is reserved, not
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

- **Samsung Galaxy Tab S5e**, the floor: Snapdragon 670 (2 Cortex-A75 at 2.0 GHz, 6 Cortex-A55),
  Adreno 615 (GLES 3.2, Vulkan 1.1), 4 GB (the owner's unit), 2560x1600; its last update is Android 11 (API 30), with 4 KB
  pages (*from its specifications; to confirm on the device with `adb shell getprop` and
  `getconf PAGESIZE`*). Below the mid-range target in CPU, and on an older Android: it sets the
  minimum API if it is to be supported (minSdk 30 or lower), and it is the low end the frame-rate
  levers are tried on. Adreno: no DXT, ETC2 and ASTC present.
- **POCO X7 5G**, the main device: MediaTek Dimensity 7300 Ultra (4 Cortex-A78 at 2.5 GHz, 4
  Cortex-A55), Mali-G615 MC2 (GLES 3.2, Vulkan 1.3), 1220x2712. Measured by the owner through `adb`
  (2026-10-17): Android 16 (API 36), 11.2 GiB of RAM, 4 KB pages, no DXT/S3TC in GLES or Vulkan
  (`textureCompressionBC` 0), ETC2 and ASTC (LDR and HDR). It decides the mid-range measurements
  (A5).

The Tab S5e is the floor (owner's decision, 2026-10-12): Android 11 and 4 GB. So **minSdk is 30
or lower** (API 30, Android 11), and the memory budget, the frame-rate levers and the converted
textures have to fit 4 GB.

A Bluetooth pad for stages A3-A4 (any Xbox or PlayStation pad), and the Auto mode of the touch
controls is tested with it connected at start and connected later.

## 7. Plan by stages

| Stage | Tickets | Needs | Validated by | Risks |
|---|---|---|---|---|
| A0. Tools and builds | AND.0 | Approved 2026-10-10 and 2026-10-17: command-line tools, JDK, NDK r30 (section 6); the SDK's Android patches (the ReXGlue agent's 34-37: Rayman's 0001 adapted, host codegen, no package registry on cross installs, the fixed 0x1000 host offset) | OGRE 14.6 static with GLES2 (`build_ogre.sh ... android-arm64`, installed in `~/android-work/ogre-install`, the backend's default there); the SDK with its `android-arm64` preset (`~/android-work/sdk-install`); the game links as `libtorchlight.so` (`android-arm64-release` preset with `-DREXGLUE_HOST_TOOL`); the SDK's PPC and unit tests pass through `adb shell` on the 4 KB tablet and phone (and the 16 KB emulator image on the Mac) | `ASharedMemory` path never run in ReXGlue; the offset on 4 KB devices |
| A1. The Android platform | AND.1 | A0 | Window, files, lifecycle and input (A1 below): a skeleton APK (our SDLActivity subclass, `libtorchlight.so`) replays a capture in a loop and survives background/foreground, screen off/on and low memory without losing the picture; the first start imports the XBLA package with SAF; saves export to and import from a zip; the log shows the folders below | Surface loss with an OGRE-owned EGL; a lost context |
| A2. OGRE GLES 3 and the backend | AND.2 | A1's APK | The backend's tests and the 20 captures replayed on the phone against Linux (PSNR); DXT decoded by capability | Precision (`mediump`), GLES format gaps, DXT memory |
| A3. Game to the main menu | AND.3 | A1-A2; the XBLA package on the phone | The game from the imported package to the main menu with a gamepad; a save made, exported and imported back. Then mods and texture replacements imported with SAF (below) | Memory; file paths and case |
| A4. Touch controls | AND.4 | A3 | An overlay drawn by the backend's UI pass, a layout editor, the Auto / Always / Never setting (below); a dungeon played by touch | Design for a pad-built ARPG on a phone screen |
| A5. Play and measure | AND.5 | A3 | Town and dungeon frame times, memory, thermals: 60 fps on the POCO X7 5G; 30 fps held on the Tab S5e, the floor, with no drops in fights | CPU too slow; throttling |
| A6. Package and CI | AND.6 | A0-A5; the owner's signing key (below) | A signed, 16 KB-aligned APK (`check_elf_alignment.sh`, `apksigner verify`); CI builds the deps and the tests without the game; the release job builds and signs the APK at a freeze and publishes it on GitHub Releases only | Developer verification (2027) |

The stages were renumbered on 2026-10-17 (owner): the SDK's build joined the tools as A0, and the
Android platform (window, files, lifecycle, input, with the skeleton APK that was A3) became A1.
The tickets follow the stages.

**A1, the Android platform** (plan approved by the owner, 2026-10-17). The game side of the build
is on the branch `android/game-build`: OGRE's render system and codec registered in code where
OGRE is static (`src/backend/ogre_plugins.h`), the NDK's cpufeatures, the game as a shared library,
and `src/platform/platform_android.cpp`, where everything below is still missing and says so.

- **A1.1 Window.** `NativeWindow` and `tl_native_window` get an Android system. The game window is
  SDL's (`SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER`, an `ANativeWindow`); OGRE's Android EGL window
  draws in it (`externalWindowHandle`, `preserveContext=true`). Landscape only.
- **A1.2 Files.**
  - Folders: the settings, the saves and the installed game in the app's internal storage
    (`SDL_GetAndroidInternalStoragePath`, deleted with the app); the logs and captures in its
    external app-specific storage (readable with `adb pull` without root); the shader cache in
    its cache (`SDL_GetAndroidCachePath`).
  - OGRE's media and `data/ui` travel in the APK and are copied to internal storage on the first
    start of each version (owner, 2026-10-17), so the backend and the menus keep reading plain
    paths.
  - **The player's game.** The APK never carries anything of the game. On the first start the
    player picks their copy of Torchlight XBLA with Android's document picker (SAF:
    `SDL_ShowOpenFileDialog`, `ACTION_OPEN_DOCUMENT` on Android; `SDL_IOFromFile` reads the
    `content://` URI). The app copies it into internal storage with the progress window and
    installs it with `game_setup::Install`, as on every platform, which checks the package before
    copying the game's files. No storage permission. Rayman's port (XDanfr/RaymanOriginsRecomp,
    `LauncherActivity.java`, read as a reference only) does the same from its own launcher: a
    zip or a folder picked with SAF, copied into app-specific storage, and the game's files
    checked afterwards (`default.xex`); it takes a folder too, through
    `ACTION_OPEN_DOCUMENT_TREE`, which SDL3's dialogs do not offer on Android. We take the package
    file only, as section 1 says.
    Done on `android/game-build` (2026-10-17): the document is copied to the app's cache with a
    progress window, checked and installed, and the copy is removed when the first start ends. So
    the first start needs free space for the package and the game's files at once; a failed copy
    says so.
  - **Saves and uninstalling.** Internal storage is deleted when the app is uninstalled. Three
    things keep the saves:
    - Export and import (the primary way): the host's options get "Export saves" (a zip of the
      saves and their backups, written where the player chooses with SAF's save dialog,
      `SDL_ShowSaveFileDialog`, `ACTION_CREATE_DOCUMENT`) and "Import saves" (a zip picked with
      SAF, replacing the current saves after a confirmation, and only from the title screen, never
      while a game holds a save). Rayman's launcher does the same ("Back up saves", "Restore
      saves").
    - `android:hasFragileUserData="true"` in the manifest: Android's uninstall dialog then offers
      to keep the app's data, and a reinstall signed with the same key finds the saves.
    - Android's Auto Backup (`allowBackup`), recommended with rules: only the saves and the
      settings (`dataExtractionRules`), never the installed game, the cache, the logs or the
      captures (backups are limited to 25 MB). It restores them on a reinstall or a new phone with
      the same Google account. That it also restores a sideloaded APK is *unverified*: tested on
      the tablet in A1.
    - Rayman's manifest sets `allowBackup="false"`; we would rather keep the saves.
- **A1.3 Lifecycle.**
  - SDL3 waits for nobody before releasing the surface of a window it does not render in (only
    its own OpenGL windows wait, `onNativeSurfaceDestroyed`). So our SDLActivity subclass gives
    SDL its own `SDLSurface` (`createSDLSurface`), whose `surfaceDestroyed` first calls a native
    function of ours. That function asks the render thread to let go of the EGL surface (OGRE's
    `_notifySurfaceDestroyed`) and waits for it, through `RunOnWindowThread`'s handoff; the surface
    comes back with `_notifySurfaceCreated`.
  - In the background the presenter blocks, so the guest's threads stop at their next present;
    audio pauses with SDL's default.
  - A lost EGL context is detected and logged; rebuilding the guest's resources is the
    prototype's question (risk 1).
- **A1.4 Input.**
  - Pads through SDL as on desktop, hot-plug included.
  - Android's Back button is the pad's Back (owner, 2026-10-17).
  - Development flags from an arguments file pushed with `adb` (SDLActivity's `getArguments`);
    releases have none.
  - Touch is A4. There is no F9 on Android: captures stay on desktop.

**Mods and texture replacements on Android** (after A3; not part of A1). From Android 11, file
managers cannot open the app's private folders, so mods are added from inside the app, never by
copying into its folder:

- a zip picked with SAF (`SDL_ShowOpenFileDialog`), unpacked into the app's mods or replacements
  folder, as the desktop installs one;
- a folder picked with SAF (`ACTION_OPEN_DOCUMENT_TREE`, walked with `DocumentsContract` in a
  small Java helper, since SDL3 has no folder dialog on Android), copied the same way;
- the textures converted to ETC2 as they are imported, with the progress shown (section 3:
  never during play);
- later, if players ask for it: a `DocumentsProvider` that shows the app's mods folder in file
  managers, as UnleashedRecomp-Android does.

**For players: your saves on Android** (text for the player docs when Android ships, A6):

> Your saves live inside the app. **Uninstalling the app deletes them**, unless you tick "Keep app
> data" in Android's uninstall dialog. Before uninstalling, or to move to another phone, use
> **Export saves** in the options: it writes a zip wherever you choose (Downloads, a memory card,
> a cloud folder). On the new install, **Import saves** from the title screen and pick that zip;
> it replaces the saves the app has. If Android's backup is on for your Google account, the app
> also backs up your saves and settings (not the game) and restores them when you install it
> again.

**A4, the touch controls setting** (owner's decision, 2026-10-10). A setting with three values,
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

**A6, the signing key** (owner's decision, 2026-10-10). The owner generates and keeps the key; the
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
  in the app's storage (A1, A3). Leaked, anyone can publish an APK that installs over ours.
- The alias (`torchlight`) and the certificate's SHA-256 fingerprint, which is not secret: the
  README publishes it so users can check a download (`apksigner verify --print-certs`), and
  Android's developer verification (2027) registers the package name with it.

The secrets of the `release` environment (Settings, Environments, `release`), set by the owner:
`ANDROID_KEYSTORE_BASE64` (the file, `base64 -i torchlight-release.p12`),
`ANDROID_KEYSTORE_PASSWORD` and `ANDROID_KEY_ALIAS`. The release job writes the file to the
runner's temporary folder, signs with `apksigner` (schemes v2 and v3), and deletes it; no other
job and no pull request sees them.

Order and reasons: A0 first because nothing runs without the SDK, and its tests run without the
game; A1 before the game because the surface and context loss is the main architectural risk and a
capture replay is enough to find it, and its APK is what A2's replays run in; A2 before the game
because replays isolate the renderer, as MAC.5 did; A3-A5 with the game; A6 last. Vulkan (with
MAC.10) after the first Android release.

**Risks, most serious first.**

1. **Surface and context loss** (section 4): OGRE owns EGL while SDL owns the activity; a device
   that drops the context would leave the backend without its mirrors of guest resources.
   Prototype in A1.
2. **CPU performance**: the game is CPU-bound on desktop; a phone may not hold 30 fps in busy
   dungeons. Measured in A5; the levers are the internal resolution, the frame cap and the
   guest-side work already profiled.
3. **Memory**: unmeasured; DXT decoding multiplies texture memory. Measured on the Mac before A2.
4. **The SDK's Android memory code** (`ASharedMemory`) never run in ReXGlue, and the host offset
   chosen at compile time for a page size known only at run time (A0; decided: a fixed 0x1000
   on every Android device).
5. **Distribution**: no Play; developer verification applies to sideloading in 2027; the signing
   key is a long-lived secret.
6. **Controls**: a pad overlay on a phone screen for a game designed for a pad; the layout needs
   play-testing (A4).

**Decided so far** (owner, 2026-10-10, 2026-10-12 and 2026-10-17):

- Devices: POCO X7 5G (main) and Galaxy Tab S5e (floor: Android 11, 4 GB) (section 6).
- minSdk compatible with API 30 (Android 11).
- Textures: never converted during play; a desktop tool converts the `pak`, mods and texture
  replacements ahead of time; on the phone, conversion only when importing or turning something
  on, from the launcher (section 3).
- Touch controls: the setting Auto / Always / Never, in the settings and the launcher (A4).
- The signing key generated and kept by the owner; CI uses it as `release` environment secrets
  (A6).
- GitHub Releases only, no Play.
- **The package name (2026-10-17, permanent): `io.github.yapel.tlrecomp`**; development builds
  `io.github.yapel.tlrecomp.dev`, which installs next to it and can be thrown away. The name shown
  under the icon stays "Torchlight Recomp". Why: the package name can never change (another name
  is another app: users would uninstall and lose their saves, and Android's developer verification
  registers it with the signing key, A6), and a neutral one does not put the whole brand in an
  identifier that cannot be changed.
- Run-time DXT (2026-10-17): decoded to RGBA with a log line, as a safety net; none appears today
  (section 3).
- A5's targets (2026-10-17): 30 fps held on the Tab S5e with no drops in fights; 60 fps on the
  POCO X7 5G (section 5).
- A2 starts at a low internal resolution (owner, 2026-10-17): the Tab S5e's screen is 2560x1600
  and its Adreno 615 is modest, so the backend draws at about 1280x800 and scales to the screen
  (the existing render scale), and the measurements start there; the native resolution is tried
  afterwards.
- The Android tools: command-line tools, NDK, the 16 KB arm64 emulator image only on the Mac
  (section 6). On the Linux machine (2026-10-17): Temurin 21 and the command-line tools unpacked
  under `~/android-sdk` (no installer), the NDK r30 in `~/android-sdk/ndk/30.0.16248370`; no
  emulator (arm64 images need an arm64 host), the Tab S5e plugged in by USB instead.
- The host offset (2026-10-17): a fixed 0x1000 on every Android aarch64 device, so one APK runs on
  4 KB and 16 KB pages; the ReXGlue agent's patch 37, with the line in `guest_host_offset.cmake`.
- Stage A1's plan (2026-10-17, section 7): OGRE's media and `data/ui` copied to internal storage on
  the first start of each version; Android's Back button is the pad's Back; the player's game
  picked with SAF and copied into internal storage; saves exported and imported as a zip.

**Resumed** (owner, 2026-10-17): A0 under way (the ReXGlue agent's SDK patches, the game side on
`android/game-build`), A1's plan approved. Measurements still pending from the pause of 2026-10-12:

1. **Memory** on this Mac: the game's footprint (Apple Silicon counts GPU memory in the process,
   as Android does) in the main menu, the dungeon standing still and fighting, and the town, with
   the standard script (`tools/input_scripts/measure-fight-town.txt`, `--dev_input_script`, a build
   with `TORCHLIGHT_DEV_COMMANDS`), 720p internal resolution, on copies (`--user_data_root`).
   Needs the fixed-floor saved game, which is not on this Mac.
2. **Run-time textures** (done 2026-10-17, section 3): none DXT, with the full map and a large
   fight included.
3. **Conversion numbers**: the texture bytes of the game's `pak` as DXT and once converted to each
   candidate (RGBA8, ETC2, ASTC), the time to convert the whole `pak` on the desktop and on the
   phones, and the quality lost (PSNR against the DXT decode). They decide the target formats and
   whether the floor device fits.

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
