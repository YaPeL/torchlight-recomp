# Patches

Patches to the ReXGlue SDK (base `bd833a2`, `development` of 2026-10-01; before it, `0c7b01a`). They are applied in
this order from the SDK checkout with `git apply <patch>`, and then the SDK is rebuilt and installed
(all configurations). They are functional fixes only; there are no observation hooks.

On Windows the SDK is checked out with LF line endings (`git clone -c core.autocrlf=false
--recurse-submodules`; the patches are LF and do not apply to a CRLF checkout), installed to
`out/install/win-amd64` with the `win-amd64` preset, and patches 3 and 15 are left out: they only
touch POSIX files (`threading_posix.cpp`, `memory_posix.cpp`). `series` marks them `posix` and
patches 17 and 18 `windows`; the build scripts apply what their platform takes. Patches 17 and 18 only change Windows
files or Windows branches, so on Linux they build nothing new. The SDK's `unit_tests` pass on Windows except
`codegen/output_stamp_test.cpp` (lines 227-228, the escaping of paths with a space or `#`), which
fails the same way on the unpatched base `0c7b01a`: an upstream issue, untouched by these patches.
On Linux the same two checks fail, and so do four `core/chrono_test.cpp` cases (the conversions
of the NT epoch, 1601-01-01), with the old base and with `bd833a2` alike; no patch touches either
file. Every other case passes on `bd833a2` with this series, the patches' own tests included.

## Numbers

A patch's number is its name in this README, the docs and the commits, and it is never reused: a
withdrawn or removed patch keeps its number (11, 19). Patches live on several branches before
they reach `develop`, so numbers are handed out in one place, this table, kept by whoever
maintains the series (ask there before giving a new patch a number). `series` lists the patches
in number order; a branch adds its own line at its number's place.

| # | Patch | Branch | State |
|---|---|---|---|
| 1-18 | (below) | `develop` | In the series |
| 11 | `rexglue-delete-on-close.patch` | | Withdrawn |
| 19 | `rexglue-mnk-keystrokes.patch` | | Removed with the move to `bd833a2` |
| 20 | `rexglue-vfs-wildcard-dos-semantics.patch` | `feature/pc-mods` | Pending integration |
| 21 | `rexglue-sdl-software-renderer.patch` | `develop` | In the series (the version with Metal on Apple, `5a98b6f`) |
| 22 | `rexglue-tests-portable.patch` | `develop` | In the series |
| 23 | `rexglue-fctiw-rounding-mode.patch` | `develop` | In the series |
| 24 | `rexglue-arm64-mffs-rounding.patch` | `develop` | In the series; confirmed on ARM64 |
| 25 | `rexglue-mtfsf-field-mask.patch` | `develop` | In the series |
| 26 | `rexglue-log-rotation.patch` | `develop` | In the series |
| 27 | `rexglue-guest-file-flush.patch` | `develop` | In the series |
| 28 | `rexglue-quiet-missing-files.patch` | `develop` | In the series |
| 29 | `rexglue-case-variants.patch` | `develop` | In the series |
| 30 | `rexglue-posix-chain-unclaimed-faults.patch` | `sdk/fault-chain` | Pending integration |
| 31 | `rexglue-guest-fatal-hook.patch` | `sdk/fatal-errors` | Pending integration |
| 32 | | | Next free number |

## The patches

1. `rexglue-vulkan-stencil-transfer.patch`: the Vulkan backend's stencil copies (without shader
   stencil export) set every bit of the destination to 1. The minimap reinterprets that EDRAM as
   color and showed red. *Bug seen only with the laptop's NVIDIA GPU; it did not happen on the
   Intel one.*
2. `rexglue-shutdown-window-lifecycle.patch`: on quitting from the game, ReXApp deleted the window
   without telling the input drivers, which then used a dangling pointer (hang or mutex error on
   exit). `src/ui/rex_app.cpp` also has to be copied to the installed `share/rexglue/` (the install
   does it).
3. `rexglue-posix-wait-fraction.patch`: the POSIX kernel's timed waits busy-polled with zero-length
   sleeps and used a whole core; now they sleep until the deadline (at most 1 ms per round). It
   affects the timing of the guest threads, audio included. *Bug seen only with the laptop's NVIDIA
   GPU; it did not happen on the Intel one.*
4. `rexglue-present-semaphore-lifetime.patch`: the Vulkan presenter reused the present semaphore
   before the presentation that consumed it had finished (`VUID-vkQueueSubmit-pSignalSemaphores-00067`,
   device lost). It uses one semaphore per image and retires each presentation with a fence. Rebased
   to apply without the present observer (dropped). *Bug seen only with the laptop's NVIDIA GPU; it
   did not happen on the Intel one.*
5. `rexglue-vulkan-compute-write-mask.patch`: compute writes to shared memory were declared as reads
   in the barriers (`VK_ACCESS_SHADER_WRITE_BIT` missing). *Bug seen only with the laptop's NVIDIA
   GPU; it did not happen on the Intel one.*
6. `rexglue-vulkan-texture-exponent-word.patch`: the SPIR-V translation of texture fetches took the
   result exponent from word 4 of the fetch constant (LOD) instead of word 3; the inventory/character
   screen came out transparent. *Bug seen only with the laptop's NVIDIA GPU; it did not happen on the
   Intel one.*
7. `rexglue-xam-exit-to-dashboard.patch`: when the game quits it asks to go back to the dashboard
   (`XamLoaderLaunchTitle` without a path). In the Debug runtime that was an `assert_always` that
   aborted the process; now it is logged and the title ends with `TerminateTitle()`, as Release
   already did. Only that case changes; every other assert stays. Not specific to any GPU.

8. `rexglue-gpu-null-plugin.patch`: adds the `rexgpu-null` plugin (`--gpu_plugin null`), for the
   `--native_live=only` mode. It is the regular `CommandProcessor` with no-ops in `IssueDraw`,
   `IssueCopy`, `IssueSwap` and `LoadShader`, and no presenter. Everything the guest expects from the
   GPU still goes through the base code: ring and read pointer, fences (`EVENT_WRITE*`, `MEM_WRITE`),
   `INTERRUPT` to the ISR, vblank, `WAIT_REG_MEM`, scratch and ZPD. The xenos plugin is unchanged.
   It creates no graphics provider (the original created a headless Vulkan one): nothing uses it
   without draws or a presenter. The unused Vulkan device kept the process holding a GPU (and its
   memory); on NVIDIA Turing and later GPUs that keeps the discrete GPU from runtime-suspending while
   the native backend draws on the integrated one. On the test laptop (Pascal, no runtime D3) there
   is no battery gain. At runtime the plugin no longer needs a working Vulkan driver (it still links
   the Xenos sources, Vulkan code included), which also simplifies other platforms.
   Origin: patches `0003-rexgpu-null-plugin-command-processing-without-GPU-em.patch` and
   `0004-rexgpu-null-no-presenter-leave-the-window-to-the-nat.patch` from Rayman Origins Recompiled
   (author BelmanteGu), taken from `android/rexglue-patches/` in the fork
   https://github.com/XDanfr/RaymanOriginsRecomp (commit `c1bf7b90e95d476e63aa4a0b077905ff6dbde9bd`;
   the original repo `BelmanteGu/RaymanOriginsRecomp` is no longer published). License: the repo is
   GPL-3.0, but its `THIRD_PARTY_NOTICES.md` distributes the ReXGlue patches under ReXGlue's terms
   (BSD-3-Clause), and `null_gpu.cpp` states it in its header. The `std::jthread`/`std::stop_token`
   workaround in `src/core/timer_queue.cpp` that came with 0003 was left out (not needed on Linux).
   Rebased on `bd833a2`: upstream moved the install target list to
   `cmake/rexglue_export_targets.cmake`, so `rexgpu-null` is added there, and the plugin gets
   `rexglue_add_version_resource` like `rexgpu-xenos`.

9. `rexglue-imgui-drawer-pending-dialogs.patch`: if the game quit with a XAM dialog open (keyboard or
   message box), the process hung: the "Kernel Dispatch" thread waits in `xeXamDispatchDialogEx` for
   the fence signaled when the dialog is deleted, `~ImGuiDrawer` did not delete pending dialogs, and
   `KernelState`'s teardown waits for that thread forever. Now the drawer deletes them when destroyed
   (their close callbacks do not run: the pending operation ends with an undefined result, with the
   title already exiting). It happened with Xenos and in only mode alike. Not specific to any GPU.

10. `rexglue-guest-vblank-rate.patch`: `GraphicsSystem::SetGuestVblankRate(hz)` so the app picks the
    guest's vblank rate (the only mode's frame rate cap; 0 = the video mode's, 60 Hz) while the
    `vsync` cvar is true. The vblank thread also sleeps until the next vblank (at most 1 ms per round)
    instead of polling every 1 ms: with the fixed poll each vblank came up to a poll late and 144 Hz
    came out as frames of 6, 7 and 8 ms. At 60 Hz the vblanks fall on the same instants as before (the
    same accumulated interval, no restart after a delay), only closer to the deadline. It affects the
    Xenos mode too. Not specific to any GPU.

11. `rexglue-delete-on-close.patch` (**withdrawn**: the file was removed, it is in the history at
    commit `bdee689`; replaced by patch 14):
    deleting a file did nothing. The guest deletes the NT way
    (`DeleteFileA` = `NtOpenFile` with DELETE access, `NtSetInformationFile` with
    `FileDispositionInformation`, `NtClose`); the SDK stored the mark (`Entry::SetForDeletion`) but
    nothing ever read it, so the file stayed. In Torchlight, deleting a character from the "continue
    game" menu (`CContinueGameMenu`, `sub_82389FF0` → `sub_8287DE58`) left its `N.TSV`. Now every
    open `File` registers itself on its entry, and the entry is deleted when its **last** open file
    closes with the mark set (NT delete-on-close semantics: with other handles open, it waits). It
    runs in `File`'s base destructor, after the derived file closed its host handle. An entry
    destroyed while files are still open on it (`VirtualFileSystem::DeletePath`, the invalidation of
    a host file that disappeared, `kSuperscede`) detaches them (`File::entry()` becomes null) instead
    of leaving a dangling pointer. Test: `tests/unit/core/vfs_delete_on_close_test.cpp` (one handle,
    two handles, unmarked, mark cleared, entry deleted with a file open), in the SDK's `unit_tests`
    (`-DREXGLUE_BUILD_TESTS=ON`; on base `0c7b01a` the unit tests also need
    `-DCMAKE_CXX_FLAGS=-I<sdk>/thirdparty/xxHash` because `hash_test.cpp` does not find `xxhash.h`, an
    existing problem unrelated to this patch; not needed from `bd833a2`, upstream `b5e0cf8`). Not specific to any GPU or
    render mode. **Candidate for an upstream report to ReXGlue**: the bug is in the base SDK
    (`src/kernel/xboxkrnl/xboxkrnl_io_info.cpp` marks, `src/system/xfile.cpp` never acts on it) and
    affects any title that deletes files.

    **Withdrawn on 2026-10-04.** During its first in-game test every character was deleted. The
    deletion itself came from the game's "Corrupt/Damaged Save" dialog answered "Yes" (it calls
    `XamContentDelete`, which removes the whole save container). The "damaged" flag was raised by
    a character imported from PC whose quest state did not match the 360 quest data (fixed in
    `tools/save_convert/`), not by this patch. Redone as patch 14, with NT's
    `STATUS_DELETE_PENDING` semantics and together with a fix for `Entry::Rename` (patch 13).

12. `rexglue-content-delete-backup.patch`: before `ContentManager::DeleteContent` and
    `UnmountAndDeleteContent` remove a content package (`XamContentDelete`, or `XamContentCreate`
    with `CREATE_ALWAYS`/`TRUNCATE_EXISTING`), the package directory and its `.header` are copied to
    `<user_data_root>/save-backups/<UTC yyyymmdd-hhmmss>-<package name>/` (a `-2`, `-3`… suffix if
    that name exists). If the copy fails nothing is deleted and the call returns
    `X_ERROR_ACCESS_DENIED`; a package that does not exist makes no backup. A safety net for Torchlight,
    where answering "Yes" to "Corrupt/Damaged Save" deletes the container with every character. Works
    the same with Xenos and in only mode. Test: `tests/unit/system/content_delete_backup_test.cpp`
    (backup then delete, failed backup keeps the package, repeated deletions keep separate backups,
    unmount-and-delete, missing package). Not specific to any GPU. Candidate for an upstream report to
    ReXGlue (as an option). The retention is in the app, not in the patch
    (`save_import/backup_retention.h`, run at start-up before the guest): a backup is removed only
    when it is both outside the newest 10 and older than 30 days (by the UTC time in its name).

13. `rexglue-vfs-rename.patch`: `Entry::Rename` took the new name from
    `std::filesystem::path::filename()` of the guest path, which on hosts where `\` is not a
    separator is the whole path (`SAVE:\1.TSV`): the renamed entry could not be found by name any
    more. In Torchlight every save renames twice (`N.TSV` → `backup.tmp`, `save.tmp` → `N.TSV`),
    so from the second save of a session the backup rename failed and the character was left
    without `backup.tmp`. Now the name is the last component of the guest path, the entry moves to
    the destination directory's entry, and NT's "replace if exists" is honoured
    (`FILE_RENAME_INFORMATION.ReplaceIfExists`, now passed to `Entry::Rename` and
    `XFile::Rename`): without it an existing destination, even a host file the entry tree does not
    know, fails with `STATUS_OBJECT_NAME_COLLISION` and nothing changes; with it the destination's
    entry leaves the tree, so two entries never share a name. A missing destination directory
    gives `STATUS_OBJECT_PATH_NOT_FOUND`. `MoveFileA` keeps its no-replace semantics. Test:
    `tests/unit/core/vfs_rename_test.cpp`. Not specific to any GPU. Candidate for an upstream
    report to ReXGlue.

14. `rexglue-vfs-delete-on-close.patch` (applies after 13): deleting a file did nothing (see
    patch 11). Every open `File` registers itself on its entry, and an entry marked for deletion
    is deleted when its last open file closes (in `File`'s base destructor, after the derived file
    closed its host handle). While it is marked and still open, opening it again fails with
    `STATUS_DELETE_PENDING`. An entry with open files is never destroyed by other paths:
    `Entry::Delete` fails, and a rename does not replace an open destination
    (`STATUS_ACCESS_DENIED`); so in normal use no open file is left without its entry. Only an
    unmount with files still open destroys their entry (before: a dangling pointer; now
    `File::entry()` becomes null). Test: `tests/unit/core/vfs_delete_on_close_test.cpp` (one and
    two handles, reopen while pending, mark cleared, open file not deleted, no replace of an open
    file, repeated safe saves deleting `backup.tmp`, delete after rename, only marked files
    disappear, unmount with a file open). Validated in the game with patch 13: characters deleted
    from the "continue game" menu disappear from the list and the disk, a save leaves the previous
    one as `backup.tmp`, and nothing else in the container changes. Not specific to any GPU.
    Candidate for an upstream report to ReXGlue.

15. `rexglue-shm-unlink-on-create.patch`: the guest memory (`xenia_memory_<ticks>`, a POSIX
    shared memory object of about 4.8 GB) was only unlinked in `CloseFileMappingHandle`, on a clean
    exit, so every run that died by a signal (a crash, `timeout`, `kill`) left the whole object in
    `/dev/shm`; enough of them filled the shared memory and later runs died with SIGBUS. Now
    `CreateFileMappingHandle` unlinks the name as soon as the object is sized: nothing opens it by
    name again (every view is mapped through the descriptor), and the system frees the memory when
    the last descriptor and view go away, however the process ends. The later `shm_unlink` in
    `CloseFileMappingHandle` finds no name and does nothing. Validated in the game: a run killed
    with SIGKILL leaves nothing in `/dev/shm` (during the run the process maps the object as
    deleted), and runs with Xenos and in only mode reach the main menu as before. Not specific to any
    GPU. Candidate for an upstream report to ReXGlue.

16. `rexglue-sdl-keystroke-repeat.patch`: the SDL input driver's keystroke repeat (400 ms delay,
    then every 100 ms) did not check that the repeated button was still pressed, and it ran before
    the button changes were looked at. A guest that stopped reading keystrokes for more than 400 ms
    after a press (a loading screen) got, on its next read, a repeated down of a button released
    meanwhile, and only then its up: in Torchlight a phantom A that picked the first character on
    entering "load character" or skipped the story screen on entering the mine. Now a button repeats
    only while it is held, as XInput does; a released one gets its up in that same read. The
    repeat state machine moved to `include/rex/input/keystroke_repeat.h` (no SDL in it). Test:
    `tests/unit/input/keystroke_repeat_test.cpp` (press and release, delay and rate, release while
    nobody reads, repeat stops on release, two buttons, ups before downs, unreportable bits), in
    the SDK's `unit_tests`. Affects Xenos and only mode alike. Not specific to any GPU. Candidate
    for an upstream report to ReXGlue.

17. `rexglue-win-timer-resolution.patch` (Windows only): Windows' timer ticks every 15.625 ms by
    default, and `rex::thread::Sleep` called `::Sleep` with whole milliseconds. A guest `Sleep(1)`
    (`XThread::Delay`) and every wait timeout lasted up to 15.6 ms, a wait under 1 ms became a
    yield, and the vblank thread of patch 10 delivered its vblanks in bursts: measured at 120 Hz,
    a median interval of 14.9 ms with 254 of 597 vblanks less than 0.5 ms after the previous one; at
    60 Hz a p99 of 30.9 ms. Now the process asks for a 1 ms timer resolution (`timeBeginPeriod(1)`
    at static initialization of `threading_win.cpp`, `timeEndPeriod` at exit), and `Sleep` and
    `AlertableSleep` wait on a per-thread waitable timer created with
    `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION` (Windows 10 1803 and later), with the requested
    microseconds; without that timer they fall back to `::Sleep`/`SleepEx`. With the patch the
    vblank interval at 60, 120 and 144 Hz has its mean on the target, its median within 0.2 ms
    and its p99 0.4 to 0.7 ms above it, with no bursts and the thread at 0-1 % of a core. Test: `tests/unit/core/threading_win_test.cpp` (1 ms and 0.5 ms sleeps,
    long sleeps, alertable sleep with and without an APC, plain Win32 sleeps and wait timeouts
    under the process resolution), built only on Windows; without the patch 5 of its 6 cases fail.
    Not specific to any GPU or render mode. Candidate for an upstream report to ReXGlue.

18. `rexglue-win-sdl3-shared.patch` (Windows only): SDL3 was built static on every platform. On
    Windows `rexruntime` is a DLL that exports only its own objects' functions
    (`WINDOWS_EXPORT_ALL_SYMBOLS`), not the static libraries it links, and its package passes
    `SDL3::SDL3-static` on to every executable: the process had two SDLs, the runtime's (video up,
    the game window, the gamepads) and the executable's (nothing initialised). The game's own SDL
    calls (`platform_sdl.cpp`: the video driver, the game window, the display modes, the gamepad;
    the SDK's `windowed_app_main_sdl.cpp` too) went to the empty one, so only mode found no window
    and the screen stayed black while the game ran with sound. Now SDL3 is a DLL on Windows
    (`SDL_SHARED` on, `SDL_STATIC` off, under `if(WIN32)`): the package exports
    `SDL3::SDL3-shared` (`SDL3.dll`, `SDL3d.dll` in Debug, `SDL3rd.dll` in RelWithDebInfo) and the
    runtime and the executable share it; elsewhere SDL stays static (on Linux the runtime's exported
    symbols already give the process one SDL). **The Windows package must ship `SDL3.dll` next to
    the executable** (the build copies it there with the other runtime DLLs). Validated: the
    executable imports SDL from the DLL and no longer links SDL's own dependencies (`SETUPAPI`,
    `IMM32`, `VERSION`); the project's tests and the SDK's `unit_tests` pass as before. Not specific
    to any GPU. Affects every title that calls SDL itself on Windows: candidate for an upstream
    report to ReXGlue. In `series` as `windows` since 2026-10-06 (before, it was applied by hand on
    the Windows machine only): `tools/deps/build_sdk.sh` skips it, and on Linux it would change
    nothing anyway (its `else()` branch is the unpatched SDL setting). Adding it changed the Linux
    dependency key (`tools/deps/key.sh` hashes `series`) once, not the Linux SDK.

19. `rexglue-mnk-keystrokes.patch`: the keyboard's controller emulation (`--mnk_mode=true`, the MnK
    input driver) reported the emulated pad's state but never a keystroke: its
    `GetDeviceKeystroke` read a queue nothing filled. Torchlight's menus read keystrokes (the same
    path as patch 16's phantom A), so with a keyboard the game could not get past its first menu,
    on Windows and Linux alike; a gamepad, through the SDL driver, was fine. Now the MnK driver
    makes keystrokes from the emulated pad the same way the SDL driver does from a real one: the
    buttons, the triggers and each stick's eight directions, with patch 16's repeat (400 ms, then
    every 100 ms, only while held), and nothing while the window has no focus or an overlay has the
    input. The virtual key table and the sticks' directions moved from the SDL driver to
    `include/rex/input/gamepad_keystrokes.h`, which both drivers use (the SDL driver's behavior is
    unchanged). Test: `tests/unit/input/mnk_keystroke_test.cpp` (a key's down and up, a held key
    without repeat before the delay, stick keys and a diagonal with the up before the down, no
    keystrokes while the emulation is off), in the SDK's `unit_tests`. Checked in the game on
    Windows: the keyboard drives the menus. Not specific to any GPU. Candidate for an upstream
    report to ReXGlue.

    **Removed with the move to `bd833a2`** (the file is in the history): upstream fixed it (issue
    #310, PR #311) and reworked it in `3f34ffc` (keystrokes for bound keys, released on focus loss,
    plus a keyboard passthrough mode). Upstream does not repeat a held bound key; if the menus need
    it, that is an upstream PR on top of patch 16's `keystroke_repeat.h`.

20. Taken by `rexglue-vfs-wildcard-dos-semantics.patch` (the mods' `*.*` wildcard), on branch
    `feature/pc-mods`: described there.

21. `rexglue-sdl-software-renderer.patch`: the SDK builds SDL with `SDL_RENDER` off, so
    `SDL_CreateRenderer` fails ("SDL not built with rendering support") and nothing can draw a
    window before the runtime's presenter exists. The first start's progress window
    (`platform::ProgressWindow`) never opened because of it (v0.1.0-beta installs without a
    progress bar), and the launcher (`docs/launcher.md`) draws ImGui with `SDL_Renderer`. Now
    `SDL_RENDER` is on with the software driver only: the Direct3D 9/11/12, GPU, Metal and Vulkan
    render drivers stay off (OpenGL and OpenGL ES were off already), so no graphics library is
    loaded before the presenter or the backend picks the GPU. Every platform, but for Metal on
    macOS: Cocoa has no window framebuffer of its own, so SDL shows a software renderer's frames
    through a GPU texture (`SDL_CreateWindowTexture`), and with every GPU driver off the software
    renderer fails there ("Window framebuffer support not available"). Apple Silicon has one GPU,
    so nothing is chosen too early (checked on macOS arm64: `launcher_imgui_test` and
    `launcher_window_test` pass with Metal on and fail without it). Size (Release):
    Windows `SDL3.dll` 2 149 888 -> 2 319 360 bytes (+169 472, +7.9%), `rexruntime.dll` unchanged
    (8 098 304); Linux (ubuntu:22.04, `tools/deps/build_sdk.sh`, where SDL is static and inside
    the runtime) `libSDL3.a` 5 969 216 -> 6 228 956 bytes (+259 740, +4.4%), `librexruntime.so`
    15 865 000 -> 16 058 232 (+193 232, +1.2%); the patch applies there without patch 18. Test:
    the project's `launcher_imgui_test` draws a frame with `SDL_CreateSoftwareRenderer` and reads
    it back (it failed without this patch); the project's tests pass on Windows. Not specific to any GPU. Touches
    `thirdparty/CMakeLists.txt` only, after patch 18's SDL lines; patch 20 touches other files, so
    the two apply in either order.
    In the `bd833a2` series since 2026-10-09, as its last line, taken from `feature/launcher-imgui`
    (`5a98b6f`): it applies after 1-29 on Linux and macOS, and after the two Windows patches.

22. `rexglue-tests-portable.patch`: the SDK's tests did not build on ARM64 or macOS.
    `tests/ppc/CMakeLists.txt` passed `-msse4.1 -mssse3` to `ppc_tests` on every architecture; now
    only on x86-64, as the root `CMakeLists.txt` already does. The PPC instruction tests assemble
    `tests/ppc/asm/*.s` with the bundled PowerPC binutils (with VMX128), which exist for Linux and
    Windows only; the new cache variable `REXGLUE_PPC_TEST_BIN_DIR` points the build at `.bin` and
    `.map` files assembled elsewhere from the same sources (empty, the default, assembles them as
    before; a missing file stops the configure, and so does a source whose SHA-256 is not the one
    in the folder's `sources.sha256`, so a changed test cannot run against stale binaries).
    `tools/deps/build_ppc_test_data.sh` makes them, and that list, on a Linux machine. `codegen_writer_test.cpp` compared two `file_time_type` inside `CHECK`, which
    makes Catch2 print them; that does not compile with Apple's libc++, so the comparison is made
    outside. Validated on Linux x86-64: `ppc_tests` passes (1462 cases) both ways, the files
    assembled by the build and the prebuilt ones are byte identical, and `[codegen_writer]` passes.
    The source check (2026-10-09): the data of `sdk/ppc-test-data` configures, and an edited `.s`
    or a missing `sources.sha256` stops the configure. Not specific to any GPU or to the game. Candidate for an upstream report to ReXGlue.

23. `rexglue-fctiw-rounding-mode.patch`: `fctiw` and `fctid` (convert in the current rounding mode)
    were emitted as `simde_mm_cvtsd_si32`/`_si64`. Without native SSE2 (ARM64) SIMDe implements
    them with C `round`, half away from zero whatever the guest's FPSCR[RN]: 2.5 gave 3 under round
    to nearest, toward zero and down. Now they call `rex::ppc::cvt_f64_s32_current` /
    `_s64_current` (`include/rex/ppc/intrinsics.h`): the same SSE conversion when SSE2 is native,
    `std::nearbyint` (which honours the mode `storeFromGuest` set) with the same out-of-range result
    elsewhere. `fctid` and `fctidz` also saturated one value late: their bound was
    `> double(LLONG_MAX)`, and `double(LLONG_MAX)` is 2^63, so 2^63 itself was converted and gave
    INT64_MIN; the bound is now `>=`, as `fctiw` has with `INT_MAX`. Test:
    `tests/ppc/asm/instr_fctix_rounding.s`, 20 cases (2.5, -2.5, 3.5, 2.7 and -2.7 under each mode,
    `fctiw` and `fctid`; `fctid` and `fctidz` of 2^63). On x86-64 the rounding cases pass with and
    without the patch (native SSE2 was right) and the two of 2^63 fail without it; the portable path was checked with a scratch program built with
    `-DSIMDE_NO_NATIVE` (the old conversion wrong in 4 to 6 of 11 cases, the helper in none), and
    the PPC tests fail without the patch on ARM64 only (`docs/rexglue-upstream.md`, section 8).
    Torchlight uses `fctid` in 10 places. Not specific to any GPU. Upstream draft D22.

24. `rexglue-arm64-mffs-rounding.patch`: `mffs` read the rounding mode back with MXCSR's order on
    every host (`FPSCRRegister::HostToGuest`); ARM64's FPCR has up and down the other way round, so
    `mffs` reported up as down and down as up, and a save and restore of FPSCR flipped them. The
    table moved into each `FPSCRPlatform` (`include/rex/platform/fpscr.h`) next to `GuestToHost`,
    and a `static_assert` in `include/rex/ppc/context.h` checks on every host that reading back
    gives what was written. Test: `tests/ppc/asm/instr_mffs_rounding.s` (each mode read back; a
    save, switch and restore of round up and of round down). Found by reading the code; on x86-64
    the tests pass with and without the patch, and the failure without it is to be seen on ARM64
    (`docs/rexglue-upstream.md`, section 8). Torchlight has 5 `mffs`. Upstream draft D23.

25. `rexglue-mtfsf-field-mask.patch`: `mtfsf` with a partial field mask wrote the wrong FPSCR fields
    on every architecture: FM bit 0 (field 0, `0xF0000000`) was mapped to the low nibble, which
    holds RN, so `mtfsf 1,f1` did not change the rounding mode and `mtfsf 0x80` did. Now the mask
    follows PowerPC bit order (`src/codegen/builders/system.cpp`, `build_mtfsf`). Test:
    `tests/ppc/asm/instr_mtfsf_fields.s`, 4 cases, all 4 failing without the patch on x86-64. With
    patches 22-25 the whole `ppc_tests` passes on Linux x86-64 (1492 cases with 23's two cases of
    2^63) and `unit_tests` is as before. Torchlight only uses the full mask (`mtfsf 0xFF`, 5 places). Upstream draft D24.

26. `rexglue-log-rotation.patch` (needs `bd833a2`): upstream `b971840` replaced the rotating log file
    sink with a plain one and removed `log_max_file_size_mb` and `log_max_files`, so a run's log had
    no size limit (a fault loop logs every retry; on this machine such runs wrote tens of
    megabytes in seconds). The cvars and the rotating sink are back as they were on `0c7b01a`
    (5 MB, 20 files by default), through `rex::detail::MakeLogFileSink`, so that our log limits
    (`src/live/log_budget.h`: 5 MB x 10 a run) work the same on both bases. The runtime's
    directory budget is untouched. Test: `tests/unit/core/log_rotation_test.cpp` (4 MB written
    with 1 MB and 2 rotations: three files, at most 3 MB; the cvars are put back however the test
    ends). Not specific to any GPU. Candidate for
    an upstream report (the removal looks unintended next to the new directory budget).

27. `rexglue-guest-file-flush.patch`: a guest's request to write its files through to the disk did
    nothing: `NtFlushBuffersFile`, the C library's `FlushFileBuffers` and `XamContentFlush` returned
    success without flushing, `XamContentClose` only unmounted, and nothing on a guest path called
    the host `FileHandle::Flush`. A power loss or a crash right after a save could lose a save the
    game believed was on disk; that affects every title that saves. Now:
    - a content package is committed as a whole, as on the console: its device
      (`HostPathDevice::EnableChangeTracking`) remembers the host files written, created or renamed
      into place since it was mounted, and the folders whose entries changed (a file created,
      renamed or removed); `XamContentFlush` and `XamContentClose` (before it unmounts) flush them
      (`HostPathDevice::FlushChanges`: each file reopened by path, since its handles are usually
      closed by then, then the folders) and log `Content <root>: flushed N files and M folders to
      disk` (at WARN with the count that failed); a failed flush is their result, and
      `XamContentClose` unmounts either way; a root that is not open still returns success, as
      before; a renamed file or folder moves what is remembered at or under it to its new path.
      Returning the failure was checked against what Torchlight does with it (2026-10-09): its one
      `XamContentClose` (thunk `sub_8287E5D8`) is called only from `sub_823AC7B8`, which turns it
      into 1 or 0, and none of that wrapper's 20 call sites reads it (17 overwrite `r3` first;
      `sub_823AC088` and `sub_821FF750`, `CSettingsMenuXenon`'s slot 3, hand it back to callers
      that do not read it either). Its one `XamContentFlush` is in Microsoft's telemetry library
      (`DataFile`, `sub_828A00A8`), which logs a failure and passes it to its own completion
      callback. The "Corrupt/Damaged Save" dialog, whose "Yes" is the game's only
      `XamContentDelete`, comes from a flag that short reads raise (`docs/saves-research.md`), not
      from either result. Had a path led there, the failure would have stayed in the log only;
    - `NtFlushBuffersFile` and `FlushFileBuffers` on a handle that is not a file now fail
      (`X_STATUS_INVALID_HANDLE`, 0); they always succeeded before. No case of it in Torchlight;
    - `NtFlushBuffersFile` and `FlushFileBuffers` flush the handle's file (`XFile::Flush`, the VFS
      `File::Flush`; a handle without write access has written nothing and returns success);
    - `FileHandle::Flush` returns whether it worked, and `rex::filesystem::FlushFolder` flushes a
      folder's entries: `fsync` on POSIX, `fcntl(F_FULLFSYNC)` first on Apple (`fsync` there stops
      at the drive's cache) with `fsync` when it fails; `FlushFileBuffers` on Windows, where folder
      entries are journaled by NTFS and are not flushed.
    Torchlight's case, the reason for this design: the first version flushed only the files still
    open at `XamContentFlush`/`XamContentClose`, and a guided save under `strace` showed no `fsync`
    at all. The game's one `NtFlushBuffersFile` call site is not on its save path, and by the
    content flush it has closed what it wrote. Its save replaces the character file by name
    (`4.tsv` before, `4.TSV` after, nothing else changed). Flushing every file on close was the
    other option; it does not depend on the game closing the content, but it would force the disk
    on every temporary file of every title, which the console does not.
    Test: `tests/unit/core/vfs_flush_test.cpp` (a writable handle's flush reaches its host handle, a
    failure is reported, a read-only handle flushes nothing; without tracking a device flushes
    nothing; files created or rewritten through closed handles are flushed with their folder and
    then forgotten; Torchlight's replace: a temporary written, the old file removed, the temporary
    renamed to `4.TSV`, one file and one folder flushed; a removed file leaves its folder only; a
    file written in a folder renamed since is flushed under the new name). The
    exports themselves are one-line calls into those, checked in a game save rather than by a
    unit test, since they need the kernel state. Checked in a guided save-and-exit under `strace`
    (2026-10-08): the `XamContentFlush` flushed `sharedstash.bin` (`fsync` 68 ms); the game then
    wrote `save.tmp` and renamed `4.tsv` to `backup.tmp` and `save.tmp` to `4.TSV`; the
    `XamContentClose` 5 s later flushed 2 files and the folder (`4.TSV` 69 ms, `backup.tmp` 0.04 ms,
    the folder 1.2 ms), all on the guest's main thread: about 140 ms per save-and-exit. An `fsync`
    on ext4 commits the journal and waits for the data it orders, so its time depends on what else
    is dirty on the system, not on the file: a 112 KB file took 1.9 ms with nothing else dirty,
    11-36 ms with 16-256 MB of other dirty data, 85 ms as the first file of a new folder. Torchlight
    commits at zone changes (under the loading screen), in the options menu and at save-and-exit
    (a guided run, 2026-10-09), never in open play; Alric's completion event is not verified yet
    (D27). The owner's criterion: synchronous is fine with a menu or a loading screen open; a commit
    in open play would need the flushes on a worker thread first. Not specific to any GPU.
    Candidate for an upstream report (D27 in `docs/rexglue-upstream.md`): it affects every title
    that saves.

28. `rexglue-quiet-missing-files.patch`: `NtCreateFile` logged every failed open at WARN, including
    a path that does not exist, which is how titles probe for optional files. With a PC mod pack
    (29 mods) Torchlight looks each data file up in every mod's folder: about 124,000 lines
    `[NtCreateFile] FAILED: path='tlmods:\<mod>\MEDIA\...' -> 0xc000000f` in the first 45 s of
    startup, 20 MB of log before the game was usable (2026-10-09, the mods agent). The duplicate
    filter planned in `docs/crash-handling.md` would not catch them: each line has its own path.
    Now `X_STATUS_NO_SUCH_FILE`, `X_STATUS_OBJECT_NAME_NOT_FOUND` and
    `X_STATUS_OBJECT_PATH_NOT_FOUND` are logged at DEBUG (off by default, `log_level` is `info`)
    and every other failure stays at WARN. `NtOpenFile` goes through the same code. No unit test:
    the export needs the kernel state; the check is the mod pack's startup. Checked there on
    2026-10-09 (the mods agent, the local SDK install with 1-28): 1.13 MB of log in the first 45 s
    instead of 14.5 MB, and 95,675 `[NtCreateFile] FAILED` lines down to one, an access denied
    (`game:\appdata` -> 0xc0000022) at startup. Not specific to any GPU. Upstream draft D28.
29. `rexglue-case-variants.patch`: names in one host folder that differ only in case (`0.tsv` and
    `0.TSV`, from a copy made by hand or an import with another case) are one name to the guest.
    `Entry::GetChild` finds the first one the host listed, and an enumeration lists both, so a
    title can show one save twice and never open the other. Two changes:
    - `Entry::Rename` with replace took only the first match as the replaced entry. When it was
      spelled differently from the new name, the host rename wrote the new spelling as a second
      file on a case-sensitive host, the old file left the tree but stayed on disk, and it came
      back at the next mount. Now every sibling the new name matches is replaced (each still has
      to be a file with no open handle), and `HostPathEntry::RenameEntryInternal` removes each
      one's host file after the rename succeeded, unless it is the same file as the destination.
      That check (`std::filesystem::equivalent`) is what keeps the new save on a case-insensitive
      host (Windows, macOS's default APFS), where the rename already replaced the variant and its
      path now names the new data. A removed file is reported to patch 27's tracking. If the
      removal fails, the rename still succeeds and the entry still leaves the tree, so the old
      file comes back at the next mount, now with the mount's WARN and one from the rename.
    - The mount logs one WARN per group of case variants in a folder. Both stay in the tree.

    Tests in `vfs_rename_test.cpp`: a rename onto a case variant (runs on every host, and is the
    real case-insensitive check on Windows and macOS); the same on a simulated case-insensitive
    host (Linux: after the mount the replaced spelling becomes a link to the destination, so a
    wrong removal deletes the link and the test fails); two variants at mount, with the WARN, a
    rename onto both and Torchlight's save sequence. Without the fix, 2 of the 3 Linux cases fail
    (8 checks); with the equivalence check taken out, the simulated case, the two-variant case and 2
    older rename cases fail (an ordinary replace would delete the save). Upstream draft D30.
30. `rexglue-posix-chain-unclaimed-faults.patch`: on Linux and macOS, a fault no SDK handler
    claimed made `ExceptionHandlerCallback` return, so the instruction ran again and faulted
    again, forever: a crash became a hang at 100 % of a core. For a guest address outside the
    physical heaps, `Memory::AccessViolationCallback` logged `Unhandled guest access violation` on
    every retry (a mods run wrote 6010 such lines in 89 s, `docs/crash-handling.md` section 1).
    Now an unclaimed fault goes to the handler installed before the SDK's (`sa_sigaction` or
    `sa_handler`), which is where our crash reporter goes (CR.3). With none, or with `SIG_IGN`,
    the signal's default action is restored, so the instruction faults once more and the process
    ends with that signal. The SDK's handler stays installed (restoring the old `sigaction`
    instead would leave the next MMIO access unhandled). Windows needs nothing: the vectored
    handler already returns `EXCEPTION_CONTINUE_SEARCH`. Tests in `unclaimed_fault_test.cpp`,
    each case in a new process (`tests/unit/child_process.h` runs a hidden case of `unit_tests`,
    so no handler an earlier test installed is in it; no core file): an unclaimed read of
    address 16 ends by SIGSEGV; a previous handler gets the fault after the SDK's handlers saw it
    once; a handler that fixes the page and claims the fault still lets the write succeed.
    Without the fix the first two hang until a 3 s alarm. POSIX only. Upstream draft D25.

31. `rexglue-guest-fatal-hook.patch`: the guest's fatal paths called `rex::debug::Break()`. On
    POSIX its first call returns (its SIGTRAP handler only resets the signal), so the guest ran on
    past a call that does not return; a second one killed the process by SIGTRAP with no message.
    Now `RtlRaiseException` (every code but SetThreadName's, the C++ throw included),
    `KeBugCheck`/`KeBugCheckEx` and `DbgBreakPoint` call `rex::system::RaiseGuestFatal`
    (`include/rex/system/guest_fatal.h`). It logs the error (kind, code, parameters, record,
    guest thread, the caller's `lr`), calls the handler the app registered with
    `SetGuestFatalHandler`, with the error and the thread's `PPCContext`, and then aborts, after
    `Break()` if a debugger is attached. It never returns to the guest. On Windows `Break()` was
    `__debugbreak()`, which ends the process through WER; now it goes through the same path.
    `DbgBreakPoint` is fatal because of what Torchlight does with it: its only caller is a
    one-instruction wrapper (`sub_8287D560`) called from 1114 places, and they are OGRE's
    `OGRE_EXCEPT` calls (their description and source strings), so Runic's OGRE breaks where
    OGRE 1.7's `OgreException.h` throws. Each site builds the message ("Index out of bounds.",
    "No viewport with given zorder : ", "Bad cast from type '..."), drops it, calls the break and
    runs on with the state the error was about. In `RenderTarget::getViewportByZOrder` it then
    returns the map's end node as a viewport. 211 of the sites have no code after the call (the
    compiler took the error as the end of the path), so a return from it leaves through code the
    source never reaches. Today a first such error goes unseen and
    a second one kills the game by SIGTRAP; now the first one ends it with the call site in the
    log. Tests in `guest_fatal_test.cpp`: each export reports its kind, code and parameters to a
    registered handler (which leaves by throwing); SetThreadName is not fatal; with no handler, or
    a handler that returns, the process aborts (in a new process, POSIX). Without the change the
    first test fails and the second break kills `unit_tests` by SIGTRAP. The C++ throw path reads
    the thrown object through the kernel's memory, which the unit tests lack, so it is checked by
    reading only. Checked in the game before it went in (2026-10-10, the render agent): logging
    hooks on the four imports, confirmed in the binary's disassembly to catch every call site, saw
    no call in three scripted runs (load and quit, a new character, a fight and the town), so
    normal play reaches none of these paths. Upstream draft D31.

30. `rexglue-posix-chain-unclaimed-faults.patch`: on Linux and macOS, a fault no SDK handler
    claimed made `ExceptionHandlerCallback` return, so the instruction ran again and faulted
    again, forever: a crash became a hang at 100 % of a core. For a guest address outside the
    physical heaps, `Memory::AccessViolationCallback` logged `Unhandled guest access violation` on
    every retry (a mods run wrote 6010 such lines in 89 s, `docs/crash-handling.md` section 1).
    Now an unclaimed fault goes to the handler installed before the SDK's (`sa_sigaction` or
    `sa_handler`), which is where our crash reporter goes (CR.3). With none, or with `SIG_IGN`,
    the signal's default action is restored, so the instruction faults once more and the process
    ends with that signal. The SDK's handler stays installed (restoring the old `sigaction`
    instead would leave the next MMIO access unhandled). Windows needs nothing: the vectored
    handler already returns `EXCEPTION_CONTINUE_SEARCH`. Tests in `unclaimed_fault_test.cpp`,
    each case in a new process (`tests/unit/child_process.h` runs a hidden case of `unit_tests`,
    so no handler an earlier test installed is in it; no core file): an unclaimed read of
    address 16 ends by SIGSEGV; a previous handler gets the fault after the SDK's handlers saw it
    once; a handler that fixes the page and claims the fault still lets the write succeed.
    Without the fix the first two hang until a 3 s alarm. POSIX only. Upstream draft D25.

The observation and diagnostic patches there were before remain in the git history.
