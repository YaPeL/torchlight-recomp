# Patches

Patches to the ReXGlue SDK (`~/rexglue-sdk`, base `0c7b01a`, v0.10.0.5-dev). They are applied in
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
    existing problem unrelated to this patch). Not specific to any GPU or
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

20. Taken by `rexglue-vfs-wildcard-dos-semantics.patch` (the mods' `*.*` wildcard), on branch
    `feature/pc-mods`, not in `develop` yet: described there.

21. `rexglue-sdl-software-renderer.patch`: the SDK builds SDL with `SDL_RENDER` off, so
    `SDL_CreateRenderer` fails ("SDL not built with rendering support") and nothing can draw a
    window before the runtime's presenter exists. The first start's progress window
    (`platform::ProgressWindow`) never opened because of it (v0.1.0-beta installs without a
    progress bar), and the launcher (`docs/launcher.md`) draws ImGui with `SDL_Renderer`. Now
    `SDL_RENDER` is on with the software driver only: the Direct3D 9/11/12, GPU, Metal and Vulkan
    render drivers stay off (OpenGL and OpenGL ES were off already), so no graphics library is
    loaded before the presenter or the backend picks the GPU. Every platform. Size (Release):
    Windows `SDL3.dll` 2 149 888 -> 2 319 360 bytes (+169 472, +7.9%), `rexruntime.dll` unchanged
    (8 098 304); Linux (ubuntu:22.04, `tools/deps/build_sdk.sh`, where SDL is static and inside
    the runtime) `libSDL3.a` 5 969 216 -> 6 228 956 bytes (+259 740, +4.4%), `librexruntime.so`
    15 865 000 -> 16 058 232 (+193 232, +1.2%); the patch applies there without patch 18. Test:
    the project's `launcher_imgui_test` draws a frame with `SDL_CreateSoftwareRenderer` and reads
    it back (it failed without this patch); the project's tests pass on Windows. Not specific to any GPU. Touches `thirdparty/CMakeLists.txt`
    only, after patch 18's SDL lines; patch 20 touches other files, so the two apply in either
    order.

The observation and diagnostic patches there were before remain in the git history.
