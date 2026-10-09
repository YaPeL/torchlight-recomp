# macOS port: research

Goal: build and run the game on macOS on Apple Silicon (arm64) with the native backend
(`--native_live=only`), using the same code as Linux and Windows, and ship it as a `.app`. This is
research only, from reading this repository, the ReXGlue SDK at `0c7b01a` (and its `development`
branch), OGRE 14.6.0's source, GitHub's and Apple's documentation, and a few checks on the
development machine: a Mac mini with an Apple M2 (16 GB), macOS 26.6, the Command Line Tools 26.2
(Apple clang 17.0.0, macOS SDK 26.2). Nothing of the game has been built or run on macOS yet.

The Windows port (`docs/windows-port.md`) is the model: each area gives its state, whether it
**blocks** a first playable build, and bounded tickets (MAC.n), listed in order at the end.

## Summary

| Area | State | Blocks? |
|---|---|---|
| ReXGlue SDK | Upstream builds `mac-arm64` (its CI, `macos-26`): SIMDe for VMX128, NEON and FPCR paths, a Darwin exception handler, 16 KB page handling. Never tested there: no SDK test runs in its CI | Yes: two ARM64 conversion bugs, the memory fences of the newer base |
| Our patches | Generic or POSIX ones apply as they are; patch 18 (one SDL) is not needed (MAC.1) | No |
| x86-specific code | Almost all of it has an ARM64 path already; ours: one test flag, one `__linux__` block, the presets' `-march` | Small |
| 16 KB pages | Handled by the SDK (views at 16 KB offsets, the 0xE0000000 +0x1000 shift on the host side, a host page reconcile); write watches have bugs, but the `null` plugin arms none | No for the native mode; `guest_abi` readers must translate (5.8) |
| Render | GL3+ on Apple's OpenGL 4.1 (on Metal) works with today's backend; OGRE's Metal has no RTSS; Vulkan on MoltenVK is new in OGRE 14.6 | Yes: GL3+ is the first renderer |
| Platform module | No macOS implementation; CMake stops (`FATAL_ERROR`) | Yes |
| Dependencies | SDK vendors SDL3, FFmpeg (with a macOS arm64 config), MoltenVK; OGRE built by our script with macOS options | Yes (scripts) |
| Package | `.app` in a `.dmg` or zip; signing and notarization need a paid Apple account | Before the release |
| CI | macOS arm64 runners are free for public repositories (3 M1 cores, 7 GB RAM, 14 GB disk) | No; memory and disk for the game build are a risk |

Recommendation: port on the SDK base the `sdk/rexglue-next` branch moves to (`bd833a2`, which adds
the memory fences ARM64 needs), render with GL3+ first, ship the macOS beta unsigned like Windows
unless the owner wants to pay for a Developer ID, and evaluate Vulkan on MoltenVK after the beta
as the successor to OpenGL.

## 1. ReXGlue on macOS and ARM64

**State.** The SDK at `0c7b01a` has a complete macOS and ARM64 path, added upstream in `df2743b`
("feat: macOS Support"), `91781c0` ("feat: ARM64 support"), `5f5d7f6` (Xenos on Vulkan with
MoltenVK) and fixes after them:

- `CMakePresets.json` has `mac-arm64` (and `mac-amd64`); the top `CMakeLists.txt` enables
  Objective-C/C++ on Apple, requires AppleClang 16 or later (we have 17), uses the small code model
  on Apple (`-mcmodel=large` is x86-64 Linux only) and `@loader_path`/`@executable_path` rpaths.
  The `-msse4.1` of `rexglue_apply_target_settings` (`cmake/rexglue_helpers.cmake`) is x86-64 only.
- Its CI builds `mac-arm64` on `macos-26` with `/usr/bin/clang`, `brew install cmake ninja
  vulkan-loader molten-vk` and `-DCMAKE_OSX_DEPLOYMENT_TARGET=13.3`
  (`.github/workflows/_build-platform.yaml`), on `v*` tags and nightly. **No test runs on any
  platform** (`REXGLUE_BUILD_TESTS` is off and there is no ctest step), so the generated code's
  behavior on ARM64 is unverified upstream.
- Core: the POSIX files serve Linux and macOS, with `__APPLE__` branches: `mach_absolute_time`
  clock (`clock_posix.cpp`), `_NSGetExecutablePath`, `sysctl` for the debugger check, semaphores
  through `sem_open`, SIGUSR1/SIGUSR2 instead of real-time signals (`threading_posix.cpp`),
  `mach_vm_region` for protection queries and shm names shortened to macOS' 31 characters
  (`memory_posix.cpp`), Darwin's `ucontext` for fibers, Apple's libc++ shims (`chrono.h`,
  `string/numeric.h`).
- UI: SDL3 with the Cocoa driver forced (`windowed_app_context_sdl.cpp`), Metal and Vulkan surfaces
  on (`thirdparty/CMakeLists.txt`); the Vulkan loader and MoltenVK built from submodules and staged
  next to the executable (`_rexglue_stage_macos_vulkan_runtime`); GPU plugins load as
  `librexgpu-<name>.dylib` from the executable's folder (`gpu_plugin_loader.cpp`).
- FFmpeg is built by the SDK from its vendored source with a checked-in
  `thirdparty/FFmpeg/config_macos_aarch64.h`.

**What is missing or wrong** (read in the code; each is an SDK patch, and a candidate for upstream):

1. **Memory ordering.** At `0c7b01a` the codegen emits nothing for `sync`, `lwsync` and `eieio`
   (`src/codegen/builders/system.cpp`, `build_sync`: "x86 has strong ordering so this is a
   no-op"), and `lwarx` is a plain load. On ARM64 the CPU reorders memory, so the guest's lock
   releases and lock-free queues (the GPU ring, audio) lose their barriers. Upstream fixed it in
   `bd833a2` (`development`, 2026-10-01: `seq_cst` fences for `sync`/`eieio`, `acq_rel` for
   `lwsync`) and `7f7c92e` (`stwcx.` as `std::atomic` compare-exchange). That is the base our
   `sdk/rexglue-next` branch already rebases the series on (`docs/rexglue-upstream.md` there,
   including the measurement of the fences' cost on x86-64). **The macOS port should start on that
   base**, not on `0c7b01a`.
2. **`fctiw`/`fctid` rounding.** `build_fctiw`/`build_fctid` (`floating_point.cpp`) emit
   `simde_mm_cvtsd_si32`/`_si64`. Without native SSE2, SIMDe implements them with
   `simde_math_round` (`thirdparty/simde/simde/x86/sse2.h:2853`): round half away from zero,
   ignoring the guest's rounding mode (2.5 gives 3 on ARM64 and 2 on x86-64). The truncating
   `fctiwz`/`fctidz` are right. Fix: emit a conversion that honors the current rounding mode
   (`std::nearbyint` / `llrint`), on every architecture.
3. **`mffs` reports up and down swapped.** `FPSCRRegister::HostToGuest` (`include/rex/ppc/
   context.h:150`) is the MXCSR order (01 down, 10 up) for both architectures; ARM's FPCR.RMode is
   01 up, 10 down. `GuestToHost` (`include/rex/platform/fpscr.h`, ARM64 branch) is right. Fix:
   move `HostToGuest` into `FPSCRPlatform`.
4. **Unhandled faults spin.** `exception_handler_posix.cpp` returns from the signal handler when no
   handler claims a fault (it never chains to the previous handler), so the instruction faults
   again forever: a crash becomes a hang. All POSIX hosts; it matters more on macOS, where the
   memory reconcile below can make pages inaccessible that are accessible on Linux. Fix: chain to
   the original handler (or the default action) so a crash is a crash.
5. **SIGPIPE.** No `SO_NOSIGPIPE`/`MSG_NOSIGNAL` around `sendto` (`src/system/xsocket.cpp`): a
   send on a closed socket kills the process on macOS. Torchlight has no Xbox LIVE here; low risk.
6. **Scalar fallbacks.** The XMA decoder's and the audio conversion's SSE paths
   (`xma_context.cpp:755-815`, `include/rex/audio/conversion.h`) have scalar `#else` versions, no
   NEON; `xma_context.cpp` truncates where x86 rounds to nearest (one least significant bit of
   audio). Primitive processing and the byte-swapping copies have NEON paths.
7. **MMIO decoding.** `MMIOHandler::TryDecodeLoadStore` (`mmio_handler.cpp`) has an ARM64 decoder
   for 32-bit `LDR`/`STR` only (no pairs, 8/16/64-bit or SIMD). Real MMIO goes through the
   generated `REX_MM_LOAD/STORE` macros, so the fault path is mostly for write watches (section 3);
   if a watched access is a form it does not decode, it asserts.

**Our patches** (`patches/series`) on macOS:

| Patch | macOS |
|---|---|
| 1, 4, 5, 6 (Vulkan) | Only the Xenos plugin; apply as they are |
| 2, 7, 9, 10, 11-14, 16, 19 (generic) | Apply as they are |
| 3 `posix-wait-fraction`, 15 `shm-unlink-on-create` (`posix`) | Apply: `threading_posix.cpp` and `memory_posix.cpp` are macOS' files too |
| 8 `gpu-null-plugin` | Applies; check the plugin installs as `librexgpu-null.dylib`. Its dropped `std::jthread` workaround is not needed: the SDK's own `timer_queue.cpp` uses `std::jthread` and builds on macOS upstream |
| 17 `win-timer-resolution` (`windows`) | Not needed: POSIX sleeps have microsecond resolution. The vblank interval is measured anyway (MAC.1), as WIN.2 did |
| 18 `win-sdl3-shared` (`windows`) | Not needed on macOS (checked in MAC.1, below): an executable binds SDL's functions to `librexruntime.dylib`, which exports them, and carries no SDL of its own |

New patches the port needs: items 2-4 above (generic, upstream candidates). `series` needs no new kind: `posix` already includes macOS.

**For the render agent** (who integrates the macOS branches into `develop`):

- `feature/macos-platform` (MAC.4): `backend.cpp` gets `platform::OgreTopLevelWindow` (commit
  `7ffed22`; null on Linux and Windows, so nothing changes there), and `src/backend/CMakeLists.txt`
  the plugin names on macOS. To review when integrating.
- `feature/durable-writes` (MAC.4b) adds a flush to the settings' and the save import's writes on
  every platform, and replaces the achievements' Linux `fsync` blocks with the same calls.
- `feature/guest-memory-view` (MAC.2) changes `guest_abi`, which every platform uses: on Linux the
  offset is 0 and every access is as before, but the Linux tests and the 20 replays are to be run
  before merging (not possible on this Mac). Windows has the 0x1000 offset: the checks for the
  Windows agent are under MAC.2, section 3.
- `tools/deps/key.sh` hashes the build scripts, so two macOS branches change the deps keys although
  their Linux and Windows paths build the same as before. CI then rebuilds and republishes the
  prebuilt SDK and OGRE once, with the same contents:
  - `feature/macos-sdk` (`build_sdk.sh`, hashed for Linux and for Windows, whose `windows.ps1`
    reads the SDK commit from it): Linux key 490625dfdd0d3587 -> 96e659a70fad93b2, Windows key
    dff3a62266274a33 -> 9118838d6b3c0af9.
  - `feature/macos-ogre` (`build_ogre.sh`, hashed for Linux only): Linux key 490625dfdd0d3587 ->
    c47d61aaf82c9c0c; Windows unchanged.
  Merged together, the Linux key changes once more (it hashes both scripts). Keys computed on
  `develop` at `acadb5a` with `shasum -a 256` (macOS has no `sha256sum`, which `key.sh` calls: a
  point for MAC.9).

**For the ReXGlue agent** (owner of the series and of `sdk/rexglue-next`; decided 2026-10-08: the
macOS port uses that branch's series on `bd833a2` as it is, and changes to the series go through
that agent, not through the macOS branches). Upstream candidates found for macOS, each with the
test that shows it:

- `fctiw`/`fctid` round half away from zero on ARM64 (item 2): `build_fctiw`/`build_fctid` in
  `src/codegen/builders/floating_point.cpp` emit `simde_mm_cvtsd_si32`/`_si64`, which without
  native SSE2 use `simde_math_round`. Test: `fctiw` of 2.5 and -2.5 under each of the four guest
  rounding modes (round to nearest gives 2 and -2).
- `mffs` swaps round up and round down on ARM64 (item 3): `FPSCRRegister::HostToGuest`
  (`include/rex/ppc/context.h`) is MXCSR's order on every architecture. Test: `mtfsfi` each mode,
  then `mffs`.
- Unhandled faults re-fault forever on POSIX (item 4): `exception_handler_posix.cpp` returns
  without chaining when no handler claims the fault. Test: a read of an unmapped address in a
  child process terminates with SIGSEGV/SIGBUS instead of hanging.

Requests from the macOS port (2026-10-08), to be fixed in the series on `sdk/rexglue-next`:

- `tests/ppc/CMakeLists.txt` compiles `ppc_tests` with `-msse4.1 -mssse3` unconditionally; on
  arm64 clang rejects them. Only on x86-64, as `rexglue_apply_target_settings` already does.
- `tests/unit/codegen/codegen_writer_test.cpp:128`: `CHECK(fs::last_write_time(probe) == before)`
  does not compile with Apple's libc++ (Catch2 cannot print `file_time_type`, whose duration is
  `__int128`); `CHECK((...))` compares the same without printing the operands.
- The PPC tests' inputs, assembled on Linux: the assembler (`tools/binutils/powerpc-none-elf-*`,
  patched for VMX128; neither upstream binutils nor LLVM has it) exists only for Linux x86-64 and
  Windows. The macOS port needs, for the SDK commit of the series, every `tests/ppc/asm/*.s`
  turned into the `.bin` and `.map` that `cmake/ppc_test_pipeline.cmake` makes (in the build
  tree's `tests/ppc/bin/`), with the SDK commit and the assembler's version written next to them.
  Proposed place: a branch of their own (`sdk/ppc-test-bins`), not `develop`: they are SDK test
  build outputs, not project sources, and nothing of the game is in them. The macOS build then
  takes them in place of the assembling steps.

All three done by that agent the same day: patch 22 (`rexglue-tests-portable.patch`, on
`sdk/rexglue-next`) builds the SDK tests on ARM64 and without the PowerPC binutils, taking the
`.bin`/`.map` from `-DREXGLUE_PPC_TEST_BIN_DIR`; branch `sdk/ppc-test-data` holds them for
`bd833a2`, with their SHA-256 sums (checked here).

**Blocks:** yes: an SDK with the fences (`bd833a2`), items 2 and 3 fixed, and our series.

**Ticket MAC.1 (SDK on macOS):** `tools/deps/build_sdk.sh` takes the preset from the host (today
it hardcodes `linux-amd64`); build and install the SDK with the series on `bd833a2` (after or
together with `sdk/rexglue-next`), and with `-DREXGLUE_BUILD_TESTS=ON` run its `unit_tests` and the
PPC instruction tests (`tests/ppc`) on this Mac, which upstream never runs: they are the first
check of the generated code on ARM64. Fix items 2-4 as patches with tests (an `fctiw` of 2.5 under
each rounding mode; `mffs` after `mtfsfi`; an unhandled fault terminates). Check for two SDLs.
Done when the SDK installs, its tests pass on ARM64 (or every failure is explained and shared with
x86-64), and the vblank thread's interval at 60 and 120 Hz is measured.

**MAC.1 results (2026-10-08, Mac mini M2, macOS 26.6, Command Line Tools 26.2, CMake 4.4.4):**

- The SDK builds and installs on `mac-arm64` (Release) both on `bd833a2` with `sdk/rexglue-next`'s
  series (every patch applies; that branch retired patch 19) and on `0c7b01a` with `develop`'s
  series, the latter through `tools/deps/build_sdk.sh` (branch `feature/macos-sdk`: the preset by
  host, `-DCMAKE_OSX_SYSROOT=macosx`, deployment target 13.3). A clean build takes about 4 minutes.
- CMake 4 no longer passes the macOS SDK to the compiler (`CMAKE_OSX_SYSROOT` empty by default):
  without `-DCMAKE_OSX_SYSROOT=macosx`, Apple's `clang++` finds no C++ standard library.
- This Mac's Command Line Tools kept a stale `usr/include/c++/v1` (11 entries from 2022-2023, no
  `<algorithm>`) that clang searched before the macOS SDK's libc++. A machine problem, not the
  project's: removed by hand (`sudo rm -r /Library/Developer/CommandLineTools/usr/include/c++`),
  after which the SDK builds from clean with no include path workaround. CI runners do not have
  it; a developer who sees `'algorithm' file not found` on macOS has the same leftover.
- SDK `unit_tests` (`bd833a2`, Release, ARM64): 245 cases, 240 passed, 4 skipped, 1 failed: the
  same `output_stamp_test.cpp:227-228` checks that fail on Linux and Windows. The four
  `chrono_test.cpp` NT epoch cases that fail on Linux pass here. One test did not compile with
  Apple's libc++: `codegen_writer_test.cpp:128`, `CHECK(fs::last_write_time(probe) == before)`,
  where Catch2 cannot print a `file_time_type` (its `__int128` duration is ambiguous for
  `operator<<`); run here as `CHECK((...))` in the scratch checkout. **For the ReXGlue agent**:
  an upstream candidate, test only.
- The PPC instruction tests (`tests/ppc`) could not build on macOS as they were: their assembler
  (`tools/binutils/powerpc-none-elf-as`, with `-mvmx128`) is shipped only as Linux x86-64 and
  Windows binaries, and `ppc_tests` was compiled with `-msse4.1 -mssse3` unconditionally
  (`tests/ppc/CMakeLists.txt`). LLVM's PowerPC assembler has no VMX128. Decided: the ReXGlue agent
  assembles them on Linux (above).
- With patch 22 and `sdk/ppc-test-data` (`sdk/rexglue-next` at `aec4e3a`, every patch of its
  series applied on `bd833a2`, `-DREXGLUE_BUILD_TESTS=ON -DREXGLUE_PPC_TEST_BIN_DIR=...`), clean
  build in 2.5 minutes: **`ppc_tests` passes entirely on ARM64, 1462 cases, 5757 assertions**,
  the generated code of 166 instruction files run through SIMDe on NEON. `unit_tests`: the same
  240 of 245 as above (the `output_stamp_test.cpp:227-228` failure shared with x86-64).
- The PPC tests do not cover items 2 and 3 of section 1: of the conversions they test only
  `fctiwz` (truncation, which does not depend on the rounding mode), and neither `fctiw`/`fctid`
  under a rounding mode nor `mffs`/`mtfsfi`. Those two stay as the ReXGlue agent's patches with
  their own tests, as listed above; MAC.1 is closed without them, and they are needed before the
  game runs (MAC.6).

**Patches 23-25 on ARM64 (2026-10-08, same Mac):** the ReXGlue agent's patches 23
(`fctiw`/`fctid` rounding mode, D22), 24 (ARM64 `mffs`, D23) and 25 (`mtfsf` field mask, D24),
with their PPC tests, run as `docs/rexglue-upstream.md` section 8 asks (`sdk/rexglue-next` at
`3bdc5d0`, whose series also has 26; `sdk/ppc-test-data` at `66c6bf1`, 169 `.bin`/`.map`, SHA-256
sums checked). Results:

1. Without the three fixes (`git apply -R --exclude='tests/*'` of 25, 24, 23; their tests kept),
   exactly as expected:
   - `fctix_rounding`: 18 cases, 12 fail: `test_fctiw_rounding_` and `test_fctid_rounding_` 1, 2
     (2.5 and -2.5 to nearest), 4, 5 (2.7 and -2.7 toward zero), 7 (-2.5 up) and 8 (2.5 down).
   - `mffs_rounding`: 6 cases, 4 fail: 3 (up), 4 (down), 5 and 6 (save and restore); e.g.
     `test_3` reads back 3 (down) for up. Nearest and toward zero pass.
   - `mtfsf_fields`: 4 of 4 fail.
2. **Patch 24 alone** (23 and 25 put back, 24 still out): `fctix_rounding` passes (18 of 18);
   `mffs_rounding` fails the same 4 cases as above, so the `mffs` failures are patch 24's alone;
   `mtfsf_fields` fails only `test_1`, which sets round down with `mtfsf 0x01` and reads it back
   with `mffs` (2 for 3, the swap patch 24 fixes). `test_4` (round up, read back with `mffs`)
   passes without 24: reported as observed.
3. With the whole series, applied again from a clean `bd833a2`: `fctix_rounding` 18 of 18,
   `mffs_rounding` 6 of 6, `mtfsf_fields` 4 of 4; **`ppc_tests` 1490 of 1490** (5811
   assertions); `unit_tests` 246 cases, 241 passed, 4 skipped, 1 failed: only
   `output_stamp_test.cpp:227-228`, as before (the extra case is patch 26's). Nothing else
   failed; nothing to report as a finding.

With D22 and D23 fixed in the series, MAC.6 (the game on ARM64) is no longer blocked by them.

**Patch 27 on macOS (guest file flushes, D27; 2026-10-08, branch `sdk/guest-file-flush` at
`a78c8ce`)**, as `docs/rexglue-upstream.md` section 8 asks:

1. Applied on this Mac's SDK checkout (the series through 26 on `bd833a2`, plus the launcher's
   patch 21 with its Apple fix): it applies cleanly; `unit_tests "[flush]"`: **5 of 5 pass** (52
   assertions), the last one flushing a real file on this Mac's APFS startup volume. That
   `F_FULLFSYNC` itself succeeds there, and is not the `fsync` fallback, was checked apart with a
   small program on the same volume: `fcntl(F_FULLFSYNC)` returns 0, 3-10 ms for a 200 KB file.
2. The check in a game save (`fs_usage`, the `F_FULLFSYNC` calls per save file and their time)
   needs the game running: at MAC.6.
3. Nothing unexpected so far. The
SDK installed in `~/rexglue-sdk/out/install/mac-arm64` is this series.
- One SDL: `librexruntime.dylib` exports SDL's functions, and an executable linked with the
  package's `rex::runtime` (which also lists `SDL3::SDL3-static`) binds them to the runtime
  (`nm -m`: `_SDL_WasInit (from librexruntime)`) and contains no SDL code: ld64 resolves against
  the dylib, which comes first on the link line. **Patch 18 is not needed on macOS**; checked
  again on the game's executable at MAC.6.
- MoltenVK warns that `newResidencySetWithDescriptor:error:` (macOS 15) is called without an
  availability check while the target is 13.3: only Xenos uses MoltenVK.
- The vblank interval needs a running title: measured at MAC.6.
- OGRE 14.6.0 (trial for MAC.3, not committed): GL3+ (Cocoa, `OpenGL.framework`), RTSS and STBI
  build as dylibs with `-DOGRE_BUILD_LIBS_AS_FRAMEWORKS=OFF`; the install's rpath is still
  absolute (MAC.3 sets `@loader_path`). Done in MAC.3, below.

Disk, measured: the SDK checkout with its submodules 0.7 GB (2.2 GB as `build_sdk.sh` makes it,
with the whole history), its Release build tree 0.36 GB, its install 0.1 GB; OGRE's shallow
checkout 0.22 GB, its build tree 0.11 GB, its install 0.01 GB. The game's build is measured at
MAC.6 (it needs the XEX). If it does not fit, in this order: one configuration at a time
(Release, or RelWithDebInfo only to profile); no debug information for the generated code
(`-g0` on `torchlight_recomp`, the bulk of the objects; symbols kept for our own code); remove
the SDK's and OGRE's build trees and sources once installed (`build_sdk.sh` with an empty work
directory already does); and `generated/` regenerated rather than kept for both configurations.

## 2. x86-specific code and what ARM64 needs

**SDK.** The generated code calls only `simde_mm_*` intrinsics, `__builtin_bswap*`,
`__builtin_rotateleft*`, `__builtin_clz*` and atomics; no raw `_mm_*`, `rdtsc`, `cpuid` or inline
assembly (`src/codegen/builders/*.cpp`, `resources/templates/codegen/pch_h.inja`). SIMDe
(`thirdparty/simde`) maps the SSE operations to NEON or portable code. What is architecture
specific, by place:

| Where | Function | x86-64 | ARM64 |
|---|---|---|---|
| `include/rex/ppc/intrinsics.h` | VMX128 helpers (`simde_mm_vsl`, `vslo`, `vsro`, `perm_epi8_`, `vctsxs`...) | SIMDe or SSE | SIMDe; `vsl`/`vslo`/`vsro` have NEON branches |
| `include/rex/platform/fpscr.h` | `FPSCRPlatform` (rounding, flush to zero) | MXCSR | FPCR through `mrs`/`msr` |
| `include/rex/ppc/context.h:150` | `FPSCRRegister::HostToGuest` | right | **wrong** (section 1, item 3) |
| `src/codegen/builders/floating_point.cpp` | `build_fctiw`, `build_fctid` | right | **wrong rounding** (item 2) |
| `src/codegen/builders/system.cpp` | `build_sync`, `build_lwsync`, `build_eieio` | no-op is right (TSO) | **needs fences**: fixed in `bd833a2` (item 1) |
| `include/rex/math.h` | `m128_*` helpers | raw SSE | not compiled (guarded, unused elsewhere) |
| `src/core/memory.cpp` | `copy_and_swap_*` | SSSE3 | NEON |
| `primitive_processor.{h,cpp}` | index conversion | SSE | NEON |
| `xma_context.cpp`, `audio/conversion.h` | audio sample conversion | SSE | scalar fallback (item 6) |
| `src/system/mmio_handler.cpp` | `TryDecodeLoadStore` | x86 decoder | partial ARM64 decoder (item 7) |
| `exception_handler_posix.cpp` | fault context | `ucontext` x86 | Darwin `__darwin_arm_thread_state64`, ESR for read/write |
| `include/rex/chrono/clock.h` | raw tick source | `rdtsc` disabled anyway | `mach_absolute_time` |
| `include/rex/platform.h` | `__builtin_debugtrap` | `int3` | `__builtin_trap` |

Also: the SDK's and our `mac-arm64` presets pass `-march=armv8-a`, which may override Apple
clang's default target for arm64 Macs (`apple-m1`: ARMv8.5 with LSE atomics). With plain ARMv8.0
every compare-exchange (`stwcx.` since `7f7c92e`) becomes a load/store-exclusive loop. To check
which target the compiler really uses and measure; the likely change is dropping the flag or
`-mcpu=apple-m1` (every Apple Silicon Mac is at least M1).

**This repository:**

| Where | What | ARM64 |
|---|---|---|
| `src/achievements/CMakeLists.txt:37` | `achievement_hooks_test` with `-msse4.1` | Built only on Linux (it uses a sparse 4 GB Linux arena); the flag must depend on the architecture before it is enabled elsewhere |
| top `CMakeLists.txt` | `-mcmodel=small` for the game | Already Linux x86-64 only |
| `CMakePresets.json` | `mac-base` sets `CMAKE_OSX_ARCHITECTURES=x86_64`, `mac-arm64-base` `arm64` with `-march=armv8-a` | See above; a `mac-arm64-nogame` preset is missing |
| `src/achievements/service.cpp` | `#if defined(__linux__)`: `fsync` of the state file and its directory | Not run on macOS (nor Windows): a platform `#ifdef` outside the platform module. Move the durable write to the platform module; on macOS `fsync` does not reach the disk, `fcntl(F_FULLFSYNC)` does |
| `guest_abi/xbox_memory.h` | comment: MMIO served "by decoding the faulting x86 instruction" | True for x86 only; reword |

Nothing else in `src/` or `tools/` uses intrinsics, `__x86_64__` or `cpuid`. The guest is
big-endian and every read already goes through `guest_abi` byte swaps, so nothing depends on the
host's (little-endian on both) byte order.

## 3. 16 KB pages

**State.** Apple Silicon's page is 16 KB (`hw.pagesize` 16384 on this Mac); the guest's are 4 KB,
64 KB and 16 MB. The SDK handles it (`df2743b`, read in `src/system/xmemory.cpp` and
`src/core/memory_posix.cpp`):

- **Mapping.** One shared memory object (`shm_open`, `ftruncate`) holds the guest's memory;
  `Memory::MapViewsMac` reserves the whole range with one `mmap` and maps each view over it with
  `MAP_FIXED`, at file offsets rounded down to the host page. Every view's offset is a 16 KB
  multiple except the 0xE0000000 view's (physical +0x1000, as on the console).
- **The +0x1000.** That view is therefore mapped at physical 0, and the 4 KB is added to the host
  address instead: `PhysicalHeap::Initialize` sets `host_address_offset_ = 0x1000` when the
  allocation granularity exceeds 4 KB, and the generated code adds it at compile time
  (`REX_PHYS_HOST_OFFSET` in `pch_h.inja`, `rex::memory::GuestPtr` in `xmemory.h`), on Windows (64
  KB granularity) and macOS arm64. No view is mapped at a 4 KB offset.
- **Protection.** Heaps with pages at least as large as the host's (64 KB, 16 MB) call `mprotect`
  directly. The 4 KB heaps (0x00000000, 0x90000000, physical, 0xE0000000) update their page table
  and then `BaseHeap::SyncHostPageAccess` gives each 16 KB host page the union of what its four
  guest pages need.
- **Kernel exports** (`NtAllocateVirtualMemory`, `MmQueryStatistics`, `ExAllocatePool`) use the
  guest's page sizes, never the host's: the guest sees no difference.

What a 16 KB host changes or breaks:

1. **Weaker protections in 4 KB heaps.** A read-only or no-access guest page next to a writable one
   is writable on the host. Guard pages there are not enforced (thread stacks' guards are in the 64
   KB heap). Harmless for a working game.
2. **Stricter than Linux and Windows for released memory.** In the 4 KB *virtual* heaps, a host
   page whose four guest pages are all free or only reserved becomes no-access on macOS, while on
   Linux and Windows the views stay readable and writable. A latent guest read of freed memory, or
   a host read of ours (a `guest_abi` reader following a stale pointer), would fault only on macOS,
   and with item 4 of section 1 it would hang. Options: an SDK patch that leaves such pages
   accessible as the physical heaps already do, or keep the stricter behavior and fix what
   faults. Decide when it is seen; not before.
3. **Write watches** (CPU writes that invalidate GPU caches): in the 0xE0000000 heap,
   `PhysicalHeap::EnableAccessCallbacks` and `TriggerCallbacks` decide each host page from its first
   guest page only, and the stale-protection recovery in `Memory::AccessViolationCallback` makes a
   whole 16 KB page writable. Missed invalidations or double faults. **The `null` GPU plugin arms
   no watch**: only the Xenos plugin's `SharedMemory` and `PrimitiveProcessor` register them, and
   nothing in our code does. So the native mode is not affected; Xenos on MoltenVK (a diagnostic
   mode) would be. Fix if Xenos is ever needed on macOS: use the union of the four guest pages,
   as the reconcile does.
4. Minor: `BaseHeap::Save` (savestates, unused) protects 4 KB and gets `EINVAL`; an
   `X_MEM_RESET` alone on a 64 KB heap maps private memory over the shared view on macOS.
5. Cost: every 4 KB heap allocation, protection and release runs the reconcile under a global lock,
   and every generated load and store compares against 0xE0000000. To measure against Linux on the
   same captures; nothing suggests it matters.

**Our side: `guest_abi` (docs/release-pipeline.md, 5.8).** The content snapshots already translate
through `GuestBytes` (`capture/guest_readers.cpp`, `rex::memory::GuestPtr`), but the scalar readers
of `guest_abi/ogre_layout.h` (`ReadU32` and the rest, used from 17 files) compute `membase +
address`. On macOS arm64 a read at 0xE0000000 or above would be 4 KB off. Option 1 of 5.8 fits
CLAUDE.md's rules: the readers take a small guest memory view (the base plus the translation,
built once by the caller with `GuestPtr`), so `guest_abi` keeps no SDK include and no platform
`#ifdef`, and tests keep their fake memory (a view with the offset on, to test the macOS
translation on Linux). The same view serves the writes (`WriteU32`, `WriteBytes`) and the
`memset`/`memcmp` on guest memory in `hooks/video_mode_hooks.cpp` and
`game_menu/save_import_menu.cpp`. It goes before the first game run, and it also fixes Windows,
where the same offset applies.

**Ticket MAC.2 (guest memory view):** `guest_abi` readers and writers over a guest memory view;
every caller builds it with `GuestPtr`. Done when the unit tests pass with the view's offset both
off and on (the macOS translation tested on every platform), and a Linux replay of a session is
unchanged.

**MAC.2 as built (branch `feature/guest-memory-view`, 2026-10-08):** rather than a view object
passed to the 330 calls (a change to every reader's callers and to the render agent's open
branches), the offset is a build constant: `xbox_memory::kHostOffset`, set for every target by
`cmake/guest_host_offset.cmake` (0x1000 on Windows and macOS arm64, 0 elsewhere), and
`xbox_memory::HostAddress(base, address)`, which every reader and writer of `ogre_layout.h` uses.
`hooks/guest_copy.cpp` checks `kHostOffset` against the SDK's
`rex::memory::detail::PhysicalHostOffset` with a `static_assert`, so `guest_abi` keeps no SDK
include and no `#ifdef`, and cannot drift from the SDK. The accesses that bypassed the readers
(capture snapshots, `GuestCall`, language pack, save import box, dev command, video mode name and
surface parameters, the achievements' tree walk) use `HostAddress` too. On Linux the offset is 0
and every access is `base + address` as before. Checked on this Mac: `guest_abi_layout_test`
(the translation for both offsets as constants, and the readers on both sides of 0xE0000000 with
the build's offset; also built with offset 0), the achievements tests, `guest_copy_test`, and the
changed files compiled against the SDK. Not checkable here (no `mac-arm64-nogame` before MAC.4,
no captures): the Linux tests and the 20 replays, which the render agent runs before merging.

**What the Windows agent has to check after MAC.2** (Windows has the same 0x1000 offset, so MAC.2
changes behavior there, unlike Linux; before it, every `guest_abi` read at 0xE0000000 or above
was 0x1000 off on Windows):

1. Build `windows-x64` (and `-nogame`): the `static_assert` in `hooks/guest_copy.cpp` passes,
   which confirms `TORCHLIGHT_GUEST_HOST_OFFSET=0x1000u` reached the build.
2. `ctest` in the nogame build: all as before, `guest_abi_layout_test` included.
3. The 20 captures replayed on Windows: same results as before MAC.2 (the replay reads captures,
   not guest memory, so any change there is a bug of the branch).
4. New captures of the same scenes (title, menus, town, a dungeon level), compared with the Linux
   captures of the same scenes: before MAC.2, content read through the scalar readers from the
   physical heap (vertex and index buffer descriptors, fetch constants, texture descriptors at
   0xE0000000 or above) could differ from Linux; after it, it must match. Any difference that
   remains is a finding to report, not to tune.
5. A play session of a few minutes on a copy of a save (`--user_data_root`): the game menu
   (video, language pack, save import box), the achievements toast and list, and the video mode
   rename for other aspect ratios: these write guest memory through `HostAddress` now.
6. Report whether anything that used to look wrong on Windows (and not on Linux) changed: MAC.2
   may have fixed it.

**Blocks:** for the native mode, only MAC.2. Items 2 and 4 of section 1 decide how a stray access
shows up.

## 4. Render: OpenGL, Metal or Vulkan

**OpenGL (OGRE's GL3+).** This Mac reports `4.1 Metal - 90.5`, renderer `Apple M2`, GLSL 4.10 for
a 4.1 core context (checked with a CGL context). The backend needs 3.3 and GLSL 330, so GL3+ and
the RTSS's GLSL work as on Linux and Windows. OGRE builds GL3+ on macOS with Cocoa
(`RenderSystems/GLSupport/src/OSX`, `NSOpenGLContext`); the window takes `externalWindowHandle`
(an `NSWindow` or `NSView`, `OgreOSXCocoaWindow.mm`), not `parentWindowHandle`, so the platform
module gives OGRE SDL's view (or a subview it creates). Vsync is `NSOpenGLCPSwapInterval`, Retina
scaling `contentScalingFactor`.

- Advantages: the code and the conventions module (`xbox_to_gl_conventions`) are Linux's; the 20
  reference captures can be compared with Linux's PSNR from the first day; the least work.
- Risks: OpenGL has been deprecated since macOS 10.14 and gets no new work; Apple has announced no
  removal, but it can come. Apple's GL is a translation onto Metal and has no debug output.
  **Threads:** the backend creates its OGRE window and draws on its own thread, and AppKit expects
  views (and `NSOpenGLContext`'s `setView`/`update`) on the main thread; the window creation may
  have to move to the UI thread (`CallInUIThread`) with only the drawing on the backend's. The
  shader cache: ARCHITECTURE.md's pending note (whether Apple's GL caches compiled programs well,
  else OGRE's microcode cache).

**Metal (OGRE's `RenderSystem_Metal`).** In OGRE 14.6's source (`RenderSystems/Metal`, off by
default, `OGRE_BUILD_RENDERSYSTEM_METAL`), tagged experimental since 1.12.7 because "proper
lighting and texturing support would require Metal Shader Language support in the RTSS, which is
not there yet". It still is not: 14.6's `ProgramWriterManager` registers only `glsl`, `hlsl`,
`glslang` and `glsles`. Every draw of ours is an RTSS program (`GuestTexgen`, `GuestPixelFog`,
`GuestAlphaTest`, `GuestLighting`, hardware skinning), so Metal would first need a Metal program
writer in OGRE, plus Metal versions of the backend's own programs (`OgreUnifiedShader.h` covers
Metal only "to some extent"). Not viable for this port.

**Vulkan on MoltenVK (OGRE's Vulkan render system).** "Vulkan on macOS (since 14.6)" in OGRE's
release notes: surfaces through `VK_EXT_metal_surface`, with MoltenVK at runtime. The render system
itself is experimental since 13.2; the RTSS writes Vulkan GLSL through the glslang plugin; and OGRE
warns about buffer updates (it does not hide Vulkan's asynchrony: `HBL_DISCARD` or own triple
buffering) and about updating buffers or loading textures after a frame's first draw, which our
backend does (per-frame dynamic buffers, textures created when the guest creates them). The SDK
already builds and ships MoltenVK for Xenos.

- Advantages: not deprecated; one path that could also serve Linux, Windows and the Steam Deck.
- Risks: the newest code in OGRE on the newest platform, two translation layers (our backend's
  OGRE on Vulkan on Metal), the backend never run on Vulkan, and a WIN.7-sized review of the
  conventions (depth range, clears, half pixel, origin) per render system.

**Recommendation.** GL3+ for the macOS beta: it is what the backend runs today, and it can be
validated with the replay before the game runs. Vulkan on MoltenVK after the beta, as its own
ticket, evaluated with the replay on the 20 captures (image and frame time against GL3+); it
becomes urgent if Apple announces OpenGL's removal or GL3+ performs badly here. Metal is out until
OGRE's RTSS writes Metal.

**MAC.3 results (branch `feature/macos-ogre`, 2026-10-08):** `tools/deps/build_ogre.sh` builds
OGRE on macOS with the same components as on Linux (GL3+, the RTSS with its shaders, STBI), for
arm64, deployment target 13.3, plain dylibs, rpath `@loader_path` and `@loader_path/..`,
`-dead_strip_dylibs`, no Wayland build. OGRE appends its install's absolute `lib/` to every rpath
(its `CMakeLists.txt`, also on Linux, where `$ORIGIN` comes first); on macOS the script deletes it
with `install_name_tool`, which keeps the ad hoc signatures valid (`codesign -v`). Checked here: a
clean build in 32 seconds; no absolute path in any dylib (`otool -L`, `otool -l`); the install
copied elsewhere with the original hidden loads `RenderSystem_GL3Plus` and `Codec_STBI` from a test
program (not committed), which lists "OpenGL 3+ Rendering Subsystem" and finds the PNG codec; the
install takes 9.4 MB (the work directory, temporary, about 0.33 GB while it builds). Creating a GL
window is MAC.4 and MAC.5's. On Linux the script passes cmake the same options as before (checked
with a stand-in `cmake`, the old and new script compared); it was not run on Linux here.

**Ticket MAC.5 (backend on macOS, replay):** OGRE 14.6.0 built for arm64 (MAC.3); the replay
presents in an OGRE window (`--window`) and renders the 20 reference captures, kept on this Mac
(game-derived: never in the repository), with the PSNR Linux gives. Done when the 20 captures
match Linux within the tolerance WIN.3 used, and the window creation runs on the thread AppKit
wants (no main-thread warning).

**MAC.5 results (2026-10-08, Mac mini M2, Apple M2 OpenGL 4.1):** the 20 parity captures and
their Linux reference (NVIDIA GTX 1050 Ti and Intel UHD 630, `develop` `f4fe0f2`), kept outside
the repository on this Mac, replayed with `tools/replay` from `feature/macos-platform` (`b4db48e`:
`f4fe0f2`'s render code plus MAC.4's top-level window), with the game's files installed from the
user's package by the project's own installer (`game_setup::Install`, every file's SHA-256
checked) into `~/Library/Application Support/TorchlightRecomp/game/`. The 20 replays take 23
seconds. Compared as the captures' README asks: (a) the replay's PSNR against the Xenos image
next to the reference's `psnr.csv`, (b) the macOS `render.png` against the reference's
(render against render, PSNR computed here). **MAC.5 done.**

| Captures | Draws | (a) macOS PSNR vs NVIDIA | (b) render vs NVIDIA | (b) render vs Intel |
|---|---|---|---|---|
| 3D scenes (v14 x5, v17 x2, v19town x5) | all drawn, no fallback texture | -0.03 to +0.34 dB | 45.6-52.9 dB | 42.3-55.6 dB |
| 2D screens (v16live x2, v17w, v18) | all drawn | 58.9 dB against 69.8 (Intel: 59.3) | 59.0 dB | 63.2 dB |
| v20townlive x4 (native-only: compared by render only) | all drawn | (12.9-14.6, as Linux) | 44.2-50.0 dB | 43.7-51.7 dB |

- The 3D scenes give Linux's PSNR within 0.34 dB, inside the 1 dB the README allows.
- The four 2D screens behave as on Mesa, not as on NVIDIA: 58.9 dB (Mesa 59.3, NVIDIA 69.8). Per
  channel against the Xenos image the M2 is at most 4 levels off (NVIDIA 2, Mesa 5), and 99.99 % of
  the channels are within 1 level: the driver's filtering and blending arithmetic, the same kind
  of difference the reference's notes give for Mesa. Nothing to correct.
- `--window` (v17 swap1193): the result shows in an OGRE window (`platform::OgreTopLevelWindow`,
  created on the main thread) with the same PSNR as offscreen (39.75 dB); no AppKit main-thread
  warning.
- OGRE's log has two kinds of "Validation Failed", both from the `glValidateProgram` OGRE calls
  right after linking each program (`OgreGLSLMonolithicProgram.cpp:101`), when Apple's GL
  validates against the current state; OGRE only logs them, and the link status does not depend
  on them. "No vertex array object bound": once per run, since no VAO is bound at link time.
  "Sampler error: A sampler's texture unit is out of range": exactly the 4 programs of the 26
  that mix a 2D and a cube sampler: at link time every sampler is still on unit 0, and GL does
  not allow two sampler types on one unit; OGRE sets the units afterwards. The scenes drawn
  with them give Linux's PSNR. Neither affects anything.

**Ticket MAC.10 (after the beta): Vulkan on MoltenVK** as a second render system, as WIN.7 did for
Direct3D 11.

## 5. Dependencies and tools

| Component | How it is built on macOS arm64 |
|---|---|
| ReXGlue SDK | `cmake --preset mac-arm64`, Ninja, Apple clang; `build_sdk.sh` with the preset chosen by host (MAC.1) |
| SDL3 | Vendored by the SDK (`thirdparty/sdl3`), static, Cocoa + Metal + Vulkan; see patch 18 |
| FFmpeg | Vendored by the SDK, built by its CMake with `config_macos_aarch64.h`; nothing to install |
| Vulkan loader, MoltenVK | Vendored by the SDK (submodules), built when `REXGLUE_USE_VULKAN` (on by default off Windows); only Xenos uses them. Upstream CI also `brew install`s them: to see whether the build needs that |
| OGRE 14.6.0 | `tools/deps/build_ogre.sh` with macOS changes (MAC.3): `-DOGRE_BUILD_LIBS_AS_FRAMEWORKS=OFF` (on by default on Apple: frameworks; we want plain dylibs, as on Linux), rpath `@loader_path`, no `-Wl,--as-needed` (GNU ld only; ld64's counterpart is `-dead_strip_dylibs`), no Wayland build, GL3+ on Cocoa (no EGL) |
| zlib | The macOS SDK's (`libz`), as on Linux |
| miniz | Downloaded by our CMake (pinned), as everywhere |

**Tools.** This Mac has the Command Line Tools 26.2: Apple clang 17.0.0, the macOS 26.2 SDK, `git`,
`codesign`, `notarytool`, `stapler`, `install_name_tool`, `otool`, `hdiutil`. Missing: **CMake ≥
3.25 and Ninja**. Homebrew is installed and has neither. Full Xcode is not needed for any stage of
this plan: it would add the Metal shader compiler (not used), Instruments for profiling (useful,
optional; `sample` and `spindump` come with the system) and the Xcode generator
(not used). To be proposed before installing: `cmake` and `ninja` (Homebrew, or CMake's own
package); nothing else.

**Deployment target.** The SDK's CI builds for macOS 13.3, the first whose libc++ has every C++23
library feature the SDK uses (to confirm; `std::format`'s runtime availability is the usual
reason). Every Apple Silicon Mac can run 13 or later. Proposal: 13.3 for the SDK, OGRE and the game
alike.

**Disk.** This Mac has about 25 GB free. The SDK with its submodules, OGRE, the generated code and
a game build tree per configuration will not fit many times over; one configuration at a time
(Release or RelWithDebInfo), and the space measured at MAC.6.

## 6. Package, signing and notarization

**Bundle.** `Torchlight Recomp.app`: `Contents/MacOS/torchlight` with the GPU plugins next to it
(the SDK loads them from the executable's folder), the shared libraries (`librexruntime`, OGRE, and
SDL3 if patch 18 applies) in `Contents/Frameworks` with `@executable_path/../Frameworks` rpaths,
and the data (`data/ui`, OGRE's plugins and media) in `Contents/Resources`: code signing seals
files by kind, and data files in `Contents/MacOS` break it. That needs `platform::ExecutableDir`'s
users split into a code and a resource directory (on Linux and Windows the same directory). An
`Info.plist` (identifier, minimum system version, high-resolution capable, the game controller
usage), an icon. The user folders follow Apple's conventions: `~/Library/Application
Support/TorchlightRecomp/` (settings, game data, saves), `~/Library/Caches/TorchlightRecomp/`
(shaders), `~/Library/Logs/TorchlightRecomp/`. Distributed as a `.dmg` (`hdiutil`, part of the
system) or a zip; a `check_app.sh` checks every Mach-O's dependencies with `otool -L` as
`check_zip.ps1` does.

**Signing.** On Apple Silicon every executable must carry a signature; the linker adds an ad-hoc
one, which is enough to run locally. What a downloaded app needs:

| Option | Cost | What the user sees |
|---|---|---|
| Unsigned (ad-hoc only) | Nothing | Gatekeeper refuses ("could not verify ... is free of malware"). Since macOS 15 there is no Control-click bypass: open it once, then System Settings → Privacy & Security → **Open Anyway** and the password. Or `xattr -dr com.apple.quarantine` in Terminal |
| Developer ID + notarization | Apple Developer Program, 99 USD (99 EUR) a year; notarization itself is free | Opens normally after a first-launch confirmation |

Notarization needs a Developer ID Application certificate, the hardened runtime and Apple's
notary service (`xcrun notarytool`, in the Command Line Tools), then `stapler` on the `.app` or
`.dmg`. The hardened runtime should need no special entitlement: the game generates no code at run
time (the SDK uses `MAP_JIT` only for executable mappings, which this static recompilation should
not create; to confirm), and every library is signed by the same identity. In CI it means two more
secrets in the `release` environment (the certificate as a `.p12` and an App Store Connect API key
for `notarytool`); the certificate shows the account holder's name as the developer.

**Decided (2026-10-08): the macOS beta is unsigned**, as the Windows one (5.7, decision 6), with
`SHA256SUMS` and the attestation. Gatekeeper's path is clearly worse than SmartScreen's (no button
in the first dialog), so the README and the release notes explain it step by step; if beta testers
stumble, signing is a CI change and a yearly fee, with no code change. The text goes into the
README's Installing section and into `release.yml`'s notes with the first macOS package (MAC.8),
not before, so no release announces a download it does not have:

> **macOS:** the app is not signed or notarized by Apple, so the first time macOS says it cannot
> check "Torchlight Recomp" for malicious software. That is macOS refusing apps from developers
> without a paid Apple account, not a detection of anything harmful. To open it:
>
> 1. Open the `.dmg` and drag **Torchlight Recomp** to **Applications**.
> 2. Double-click it in Applications. macOS shows the warning: click **Done** (not "Move to
>    Trash").
> 3. Open **System Settings → Privacy & Security**, scroll down to the message about
>    "Torchlight Recomp" and click **Open Anyway** (it stays there for about an hour).
> 4. Confirm with **Open Anyway** again and your password or Touch ID.
>
> macOS remembers the choice; later starts open it directly. A new version needs the same steps
> once. To check a download, compare its SHA-256 (`shasum -a 256 FILE` in Terminal) with the
> release's `SHA256SUMS`.

The exact wording of macOS' dialogs is checked on this Mac with the first `.dmg` (MAC.8).

**Ticket MAC.8 (package):** `cmake --install` into a `.app` layout, `packaging/macos/make_app.sh`,
`make_dmg.sh` and `check_app.sh`, symbols split (`dsymutil`, kept private like the Linux `.debug`
files). Done when the `.dmg` copied to another user account of this Mac (quarantined, as a
download) opens through "Open Anyway" and reaches the first start's setup, and `check_app.sh`
finds no dependency outside the bundle and the system.

## 7. CI

GitHub's standard macOS arm64 runners (`macos-14`, `macos-15`, `macos-26`, `macos-latest`) have 3
M1 cores, 7 GB of RAM and 14 GB of SSD, and are **free and unlimited for public repositories**
(the 10× minute multiplier and 0.062 USD per minute apply only to private repositories). Limits on
the Free plan: 5 concurrent macOS jobs (of 20 in total), 6 hours per job. The SDK's own CI builds
`mac-arm64` on `macos-26`.

What fits: `deps-macos` (the SDK and OGRE, published as `deps-macos-<key>` like Linux and Windows;
`key.sh macos` with the runner image as the system), and `test-macos` (the `nogame` preset and
ctest; whether the runner's virtual GPU gives OpenGL 3.3 for the GL tests is to be checked, else
they skip as on Linux). Pin `macos-26` (not `macos-latest`), as Linux pins `ubuntu-24.04`.

Risks: the game's Release build (67 MB of generated text on Linux) with 7 GB of RAM and 14 GB of
disk on 3 cores. Linux's public runners have 16 GB and 4 cores. To measure on the first release
dry run; if it does not fit, the options are fewer parallel jobs (`-j2`), GitHub's larger macOS
runners (billed per minute even for public repositories), or a self-hosted runner on this Mac
(only for the tag-triggered release job in the approved `release` environment, never for pull
requests: a public repository's self-hosted runner runs fork code otherwise). The rules of 5.4
hold: no caches or compiler caches in jobs that touch the game.

**Ticket MAC.9 (CI):** `deps-macos`, `test-macos` in `ci.yml`; `game-macos` and `check-macos` in
`release.yml` (the private XEX checkout, codegen, Release, ctest, `.app`, `.dmg`, symbols to
`torchlight-symbols/<tag>/macos-arm64/`). Done when a dry-run tag produces the `.dmg` in the draft
release with its checksum and attestation.

Also for MAC.9: `tools/deps/key.sh` pipes into `sha256sum`, which macOS does not have (it has
`shasum -a 256`; MAC.3's keys were computed with it by hand). The script should use whichever
exists (`sha256sum`, else `shasum -a 256`; both print the same digest first), so the same key
comes out on Linux, Windows (Git Bash has `sha256sum`) and macOS.

## 8. Platform module

**State.** `platform_linux.cpp` and `platform_win.cpp` implement `platform.h`;
`src/platform/CMakeLists.txt` stops on any other system. macOS needs `platform_mac.mm`
(Objective-C++: AppKit for the view) and `user_folders_mac.cpp`, with the SDL3 parts shared
(`platform_sdl.cpp`):

| Function | macOS |
|---|---|
| `NativeWindow` | New `kCocoa` system: the `NSView` (and `tl_native_window` the same value) |
| `EmbeddingVideoError`, `VideoDriver` | SDL's `cocoa` (the SDK forces it); always embeddable |
| `FindGameWindow` | `SDL_PROP_WINDOW_COCOA_WINDOW_POINTER`, its content view; size in pixels (`SDL_GetWindowSizeInPixels`: Retina) |
| `OgreWindowParams` | `externalWindowHandle` with the view, `contentScalingFactor`, vsync |
| `OgreRenderSystemDir` | the plugin directory |
| `Gpus`, `SelectGpu` | one GPU on Apple Silicon: the row stays disabled |
| `CanCreateGl33Context` | always true while GL3+ is the only render system |
| `ExecutableDir` | `_NSGetExecutablePath`, plus the resource directory of section 6 |
| `ConfigDir`, `ShaderCacheDir`, user folders | `~/Library/...` (section 6) |
| `KeyReader` | the replay window's F9: Cocoa key events, or null |
| durable file write | `F_FULLFSYNC` (section 2, `achievements/service.cpp`) |

Fullscreen is SDL's desktop fullscreen (its own Space); the menu bar and the camera notch of
laptops are SDL's to handle. Cmd+Q comes as SDL's quit event: it has to take the game's quit path.

**Ticket MAC.4 (platform module and nogame build):** `platform_mac.mm`, `user_folders_mac.cpp`,
`kCocoa`, the CMake branch and a `mac-arm64-nogame` preset. Done when the `nogame` build's ctest
passes on this Mac.

Found in MAC.3, for MAC.4 (or MAC.6 at the latest): `src/backend/CMakeLists.txt` finds OGRE's
plugins as `${OGRE_PLUGIN_DIR}/<name>${CMAKE_SHARED_MODULE_SUFFIX}`, which is `.so` on macOS, but
OGRE's plugins there are `.dylib` (`lib/OGRE/RenderSystem_GL3Plus.dylib`); the staging of
`ogre/plugins` and the install rules for the dylibs (Linux-only today) go with it.
Done in MAC.4 for the staging (the install rules stay for MAC.8).

**MAC.4 results (branch `feature/macos-platform`, 2026-10-08):** `platform_mac.mm`,
`user_folders_mac.cpp`, `NativeWindow::kCocoa`, the CMake branch, the backend's plugin names and
the `mac-arm64-nogame` preset (`mac-base` also passes `CMAKE_OSX_SYSROOT=macosx` and deployment
target 13.3). The user folders: settings, user data and the game's files in
`~/Library/Application Support/TorchlightRecomp/`, shaders in `~/Library/Caches/TorchlightRecomp/`,
logs in `~/Library/Logs/TorchlightRecomp/`. Nothing outside `platform/` and the backend's CMake
needed a change for the build: no other Linux dependency showed up. On this Mac, with the SDK on
`bd833a2`: the nogame build configures and builds with no new warnings, and ctest passes 57 of 59
(with `3bdc5d0` of `sdk/rexglue-next` applied by hand, which `develop` needs on that SDK base).

The two that fail are the GL3+ tests (`ui_pass_test`, `render_scale_test`), for a reason found,
not guessed: **OGRE 14.6's Cocoa GL window cannot create a window of its own**
(`CocoaWindow::createNewWindow` throws "Builtin Window creation broken. Use an external Window",
`RenderSystems/GLSupport/src/OSX/OgreOSXCocoaWindow.mm:628`). It only draws in an external
`NSView`/`NSWindow` (`externalWindowHandle`). The game's window is SDL's, so the game is not
affected; what is affected is every backend top-level window: the hidden 64x64 one of an offscreen
backend (the tests, the replay without a window) and the replay's visible window. Proposal, to
agree with the render agent since it touches `backend.cpp`: a platform function that makes that
window where OGRE cannot (`platform::OgreTopLevelWindow`, null on Linux and Windows, an `NSWindow`
on macOS, hidden or not, with `[NSApplication sharedApplication]` first), whose view
`tl_backend_create` passes through `OgreWindowParams` and keeps until the OGRE window is destroyed.
About 15 lines in `backend.cpp`, no change on Linux or Windows. Until then MAC.4 is done but for
those two tests, and MAC.5 (the replay) needs it.

Done (decided 2026-10-08, commit `7ffed22` on `feature/macos-platform`, for review by the render
agent): `platform::OgreTopLevelWindow` (a titled `NSWindow` on macOS, shown or hidden; null on
Linux and Windows) and its use in `tl_backend_create`. Both GL3+ tests pass on this Mac (Apple M2,
OpenGL 4.1; OGRE logs "Validation Failed: No vertex array object bound." while validating
programs, and the tests check what was drawn): **`mac-arm64-nogame` ctest 59 of 59; MAC.4 done.**

**Durable writes on macOS (for the ticket below).** On macOS `fsync` hands the data to the drive
but does not make the drive write it: only `fcntl(F_FULLFSYNC)` does (Apple's `fsync(2)` man page).
Where the project writes files that must survive a power cut:

| Where | What | Linux | Windows | macOS today |
|---|---|---|---|---|
| `achievements/service.cpp` (state) | temporary file, rename | `fsync` file and folder (`#if defined(__linux__)`) | nothing | nothing (the `#if` leaves it out) |
| `settings/host_settings.cpp` `Save` | `settings.toml` through `.tmp`, rename | nothing | nothing | nothing |
| `save_import/import_plan.cpp` `WriteAtomic` (`.RAW` and the import state) | `.import-tmp`, rename | nothing | nothing | nothing |
| `save_import/backup_retention.cpp` | only removes old backups | - | - | - |
| The game's saves and their backups | written by the SDK (VFS, `content-delete-backup`) | SDK's | SDK's | SDK's |

Proposal: a small library in the platform module with no SDK dependency, like
`torchlight_user_folders` (the achievements build on their own), `platform/durable_file.h`:
`bool FlushToDisk(const std::filesystem::path& file, std::string& error)` and
`bool FlushFolderToDisk(const std::filesystem::path& folder, std::string& error)`, one
implementation per platform: Linux `fsync`, exactly what `service.cpp` does now; macOS
`fcntl(F_FULLFSYNC)`, falling back to `fsync` where the file system does not support it
(`ENOTSUP`, e.g. some network volumes); Windows nothing, as now. Step 1, no change on Linux or
Windows: `service.cpp` calls it instead of its `#if defined(__linux__)` blocks (which also takes
a platform `#if` out of `achievements/`). Step 2, a decision for the owners because it adds
durability on Linux too: settings and `WriteAtomic` call it before their rename. The SDK's writes
of the game's saves are the ReXGlue agent's to check (a `F_FULLFSYNC` in `HostPathFile` on macOS).

**Ticket MAC.4b (durable writes, before MAC.7):** `platform/durable_file.h` and step 1; step 2
if the owners agree; done when `service.cpp` has no platform `#if` and a test writes, flushes and
reads back on each platform.

Decided 2026-10-08: steps 1 and 2, and Windows gets the same guarantee. Done on branch
`feature/durable-writes` (from `develop`, `durable_file.cmake`, `durable_file_{posix,linux,mac,win}`):
`FlushFileToDisk` (Linux `fsync`, macOS `F_FULLFSYNC` with `fsync` where a volume lacks it,
Windows `FlushFileBuffers`) and `CommitReplace` (the rename and a flush of the folder on Linux and
macOS; `MoveFileExW` with `MOVEFILE_WRITE_THROUGH` on Windows). Used by the achievements' state
(same `fsync`s as before on Linux; the `#if defined(__linux__)` is gone), `settings.toml` and the
save import's `WriteAtomic` (`.RAW` files, import state). Checked here, merged with
`feature/macos-platform`: ctest 60 of 60 (`durable_file_test`, `host_settings_test`, the save
import tests), the achievements built on their own. **To check on Linux and Windows** (not built
there): `durable_file_test`, `host_settings_test`, the `save_import_*` tests and the achievements'
tests; on Windows `durable_file_win.cpp` is new code. The game's saves (the SDK's `HostPathFile`)
are the ReXGlue agent's.

**`feature/launcher-imgui` on macOS (for the Windows agent; branch `feature/launcher-imgui-macos`
= that branch, merged with `feature/macos-platform`, plus one fix):**

- `browse_places_test` passes on macOS, its startup disk case included: the made-up
  `Volumes/Macintosh HD` link to the root is left out (on macOS the link can be made, so the case
  that could not run on Windows ran). `BrowsePlaces()` on this Mac (`platform_mac_launcher.cpp`,
  built in the macOS module) gives `/Users/<user>`, `~/Downloads`, `/Volumes/Claude` (a mounted
  volume) and `/`, without `/Volumes/Macintosh HD -> /`. `PreferFullscreenLauncher()` is false.
- `platform_mac_launcher.cpp` and `browse_places.cpp` join the macOS sources of
  `src/platform/CMakeLists.txt` (in the merge; the only conflict was that list).
- **Fix to SDK patch 21** (`rexglue-sdl-software-renderer.patch`), commit `a343dc4`: with every GPU
  render driver off, SDL's software renderer cannot open a window on macOS ("Window framebuffer
  support not available"): Cocoa has no window framebuffer of its own, and SDL3 shows a software
  renderer's frames through a GPU texture (`SDL_CreateWindowTexture`). The fix keeps
  `SDL_RENDER_METAL` on for Apple only (one GPU on Apple Silicon: nothing chosen too early); the
  patch still applies after `rexglue-win-sdl3-shared` (Windows) and without it (macOS), and its
  Windows and Linux parts do not change. With it, `launcher_imgui_test` (its frame read back) and
  `launcher_window_test` pass here; without it, both fail. The README's entry says so.
- With both branches, ctest passes 63 of 65 on this Mac: every launcher test; the two that fail
  are the GL3+ ones above.

## 9. Plan by stages

From what can be validated first (no game) to what is validated last. Each stage's risks are the
ones that can stop it; the two-failed-hypotheses rule applies to each.

| Stage | Ticket | Needs the game? | Validated by | Risks |
|---|---|---|---|---|
| 0. Tools | — | No | CMake and Ninja installed (proposed first) | None |
| 1. SDK builds and its tests pass on ARM64 (done 2026-10-08) | MAC.1 | No | SDK `unit_tests` and PPC tests on this Mac | SIMDe differences beyond items 2-3 (NaN, denormals, saturation) found by the PPC tests; the series on `bd833a2` not merged yet |
| 2. Guest memory view | MAC.2 | No | Unit tests with the offset on and off; a Linux replay unchanged | Callers that bypass `guest_abi` |
| 3. OGRE builds (done 2026-10-08) | MAC.3 | No | `build_ogre.sh` on macOS; OGRE's GL3+ plugin loads | Cocoa GL code paths less used upstream |
| 4. Platform module, `nogame` ctest | MAC.4 | No | ctest on this Mac (pure tests, `ui_pass_test` and `render_scale_test` on GL) | AppKit main-thread rules for the backend's window |
| 5. Backend on macOS GL (done 2026-10-08) | MAC.5 | Captures (local, from the user) | Replay of the 20 captures, PSNR as Linux | Apple GL differences (precision, polygon offset, sRGB, DXT small mips): each through `xbox_to_gl_conventions` |
| 6. Codegen and game build | MAC.6 | The XEX (from the user) | `rexglue codegen`, a Release and a RelWithDebInfo build link | Build time, memory and the 25 GB of free disk |
| 7. Game to the main menu | MAC.6 | Game data | Only mode with `null`: boot, title, menu, sound, gamepad; Quit exits | Memory ordering (fences), fault hangs, 16 KB reconcile making a stale access fault, two SDLs, signals (SIGUSR1/2) |
| 8. Play | MAC.7 | Game data, saves on copies (`--user_data_root`) | Town and a dungeon, saves, an F9 capture replayed on Linux with the same result, frame times against Linux | Performance (GL on Metal, reconcile cost), vblank pacing, Retina sizes |
| 9. Package and CI | MAC.8, MAC.9 | Game in CI (private XEX) | `.dmg` opens on a quarantined copy; dry-run tag | Runner memory and disk; signing decision |
| After the beta | MAC.10 | Captures | Vulkan on MoltenVK against GL3+ | OGRE Vulkan's maturity |

## Follow-up tickets, in order

1. **MAC.1 SDK on macOS:** the series on `bd833a2`, built with `mac-arm64`; SDK tests on ARM64;
   `fctiw`/`fctid` rounding, `mffs`, unhandled faults as patches; two SDLs checked; vblank
   interval measured.
2. **MAC.2 Guest memory view:** `guest_abi` readers and writers through `GuestPtr`'s translation
   (5.8, option 1); fixes Windows too.
3. **MAC.3 OGRE on macOS:** `build_ogre.sh` for macOS (dylibs, `@loader_path`, deployment target).
4. **MAC.4 Platform module:** `platform_mac.mm`, user folders, `kCocoa`, `mac-arm64-nogame`.
5. **MAC.5 Backend on macOS:** the 20 captures on GL 4.1 against Linux.
6. **MAC.6 First game run:** codegen, build, only mode to the main menu.
7. **MAC.7 Play:** town, dungeon, saves, F9 capture cross-checked, performance against Linux.
8. **MAC.8 Package:** `.app`, `.dmg`, checks, symbols.
9. **MAC.9 CI:** deps, tests and the release job on `macos-26`.
10. **MAC.10 (after the beta) Vulkan on MoltenVK.**

Owner decisions: signing (section 6: decided, unsigned beta), and whether Xenos (the emulated GPU) must work on macOS at
all: it needs MoltenVK and the write-watch fixes of section 3, and only serves diagnostics.

## Sources

- ReXGlue SDK: `~/rexglue-sdk` at `0c7b01a` and `origin/development` (`bd833a2`, `7f7c92e`);
  <https://github.com/rexglue/rexglue-sdk>.
- OGRE 14.6.0 source (`v14.6.0`): `Docs/14-Notes.md` ("Vulkan on macOS"), `Docs/13-Notes.md`
  (Vulkan), `Docs/1.12-Notes.md` (Metal preview), `Components/RTShaderSystem/src/
  OgreShaderProgramWriterManager.cpp`, `RenderSystems/GLSupport/src/OSX/OgreOSXCocoaWindow.mm`,
  `CMakeLists.txt`; <https://github.com/OGRECave/ogre>.
- GitHub Docs: GitHub-hosted runners
  <https://docs.github.com/en/actions/reference/runners/github-hosted-runners>; Actions billing
  <https://docs.github.com/en/billing/concepts/product-billing/github-actions>; limits
  <https://docs.github.com/en/actions/reference/limits>.
- Apple: notarization with a Developer Program membership has no extra fee
  <https://developer.apple.com/forums/thread/746992>; Gatekeeper on macOS 15 and the removed
  Control-click bypass <https://flaviocopes.com/apple-developer-free-vs-paid/>.
- This Mac: `sysctl hw.pagesize`, a CGL 4.1 core context's `GL_VERSION`, `xcrun --find` for the
  signing tools.
