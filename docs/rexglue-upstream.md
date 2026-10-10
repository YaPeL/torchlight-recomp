# ReXGlue: our patches against upstream

Analysis of the SDK patch series (`patches/`) and of the SDK problems noted elsewhere in the docs,
against the current upstream, with drafts for the ones worth sending. Nothing here has been posted
upstream; the drafts are for the maintainer of this repository to send.

## Method

- **Our base:** `0c7b01a` (2026-09-03, `development`, v0.10.0.5-dev) plus the 18 patches of
  `patches/series`, and `rexglue-vfs-wildcard-dos-semantics.patch` from branch `feature/pc-mods`
  (not on `develop` yet; called patch 20 below).
- **Current upstream:** `https://github.com/rexglue/rexglue-sdk`, branch `development` at `bd833a2`
  (2026-10-01, tag `nightly-20261002-bd833a2a`), 19 commits after our base. `main` only carries
  releases (`v0.10.0`, 2026-08-21, older than our base). Open PRs target `development`; the wiki
  (`Development/Contributing`) requires an issue for every PR, so each draft below is an issue
  plus a PR.
- **Still needed?** Each patch was applied with `git apply` to a clean clone of `development`, in
  `series` order, and the code it changes was read in `development`. Upstream issues and PRs were
  checked on 2026-10-08 (open list and searches by topic). Nothing was built or run against
  `development`: whether the series *works* there is part of the update cost (last section).
- The shared SDK in `~/rexglue-sdk` was only read.

Result of the apply: on `development` the whole series applies in order except patch 8
(`rexglue-gpu-null-plugin.patch`, conflict in `cmake/rexglue_install.cmake`, which upstream split
into `rexglue_export_targets.cmake`) and patch 19 (`rexglue-mnk-keystrokes.patch`, superseded,
below). Upstream only touched files of patches 2, 8, 16 and 19; in 2 and 16 the code we fix is
unchanged.

## Summary

| # | Patch | Fixes | Upstream now | Scope | Recommendation |
|---|---|---|---|---|---|
| 1 | vulkan-stencil-transfer | Stencil-bit transfers without stencil export write 0xFF | Still broken | Generic | Propose |
| 2 | shutdown-window-lifecycle | Input drivers keep a dangling window on guest-initiated exit | Still broken | Generic | Propose |
| 3 | posix-wait-fraction | Timed waits busy-poll the last millisecond | Still broken | Generic (POSIX) | Propose |
| 4 | present-semaphore-lifetime | Present semaphore reused while the presentation engine still waits on it | Still broken | Generic | Report; the PR needs a path without `VK_EXT_swapchain_maintenance1` first |
| 5 | vulkan-compute-write-mask | Compute writes to shared memory declared as reads in barriers | Still broken | Generic | Propose |
| 6 | vulkan-texture-exponent-word | SPIR-V fetch reads the result exponent from word 4 (LOD bias) instead of word 3 | Still broken | Generic | Propose |
| 7 | xam-exit-to-dashboard | Debug builds abort on `XamLoaderLaunchTitle(NULL)` | Still there | Generic | Propose |
| 8 | gpu-null-plugin | No GPU plugin without emulation | Not upstream | Generic, third-party code | Keep locally; rebase |
| 9 | imgui-drawer-pending-dialogs | Hang on exit with a XAM dialog open | Still broken | Generic | Propose |
| 10 | guest-vblank-rate | vblank up to 1 ms late (uneven 144 Hz); no API for the rate | Still there | Pacing generic; the API is ours | Propose the pacing; keep the API local unless asked |
| 11 | (withdrawn) | | | | Already gone |
| 12 | content-delete-backup | Back up a content package before deleting it | Not upstream | Policy for our users | Keep locally |
| 13 | vfs-rename | `Entry::Rename` keeps the whole guest path as the name on POSIX; no `ReplaceIfExists` | Still broken | Generic | Propose |
| 14 | vfs-delete-on-close | `FileDispositionInformation` marks a file and nothing deletes it | Still broken | Generic | Propose (after 13) |
| 15 | shm-unlink-on-create | Killed runs leave ~4.8 GB in `/dev/shm` | Open issue #445 | Generic (POSIX) | Propose, fixes #445 |
| 16 | sdl-keystroke-repeat | SDL driver repeats a released button before reporting its up | Still broken | Generic | Propose |
| 17 | win-timer-resolution | `Sleep(1)` and short waits last up to 15.6 ms on Windows | Still there | Generic (Windows) | Propose |
| 18 | win-sdl3-shared | Two SDL3 copies in one process on Windows | Still there | Generic (Windows) | Propose |
| 19 | mnk-keystrokes | MnK driver never produced keystrokes | Fixed upstream (#310, `3f34ffc`) | Generic | Drop on the next base update |
| 20 | vfs-wildcard-dos-semantics | `*.*` does not match names without a dot | Still there | Generic | Propose, with the caveat in its draft |
| 22 | tests-portable | SDK tests do not build on ARM64 or without the PowerPC binutils | Still there | Generic | Propose (D20) |

Other topics:

| Topic | Upstream now | Recommendation |
|---|---|---|
| `RtlUnwind` stub | Still a stub (`xboxkrnl_rtl.cpp`), no issue | Report (issue only) |
| Codegen: registers in `ctx`, full CR per compare | The codegen already has `cr_as_local`, `non_volatile_as_local` and the rest, off by default. Since `b0b2bbb` our manifest sets `reserved_`, `xer_`, `ctr_` and `cr_as_local` (`docs/guest-hot-paths.md`, "Codegen options") | No upstream report beyond D21 |
| `-mcmodel=large` on Linux | Still forced for the SDK and every consumer target | Propose an opt-out |
| `*.*` in the wildcard engine | Same as patch 20 | See patch 20 |
| `non_volatile_as_local` with `setjmp` (D21) | The generated `setjmp` saves only `ctx`; the localized r14-r31 are lost across a `longjmp` | Issue, for when we want that flag |
| `non_argument_as_local` and values passed in r11/r12 (D26) | The funclets' frame in r12 and the stack probe's size are lost: deadlock seen | Issue, with fix directions |
| Guest file flushes are no-ops (D27) | `NtFlushBuffersFile`, `FlushFileBuffers`, `XamContentFlush` return success without flushing | Issue and PR; our patch 27 |
| Every missing file is a warning (D28) | `NtCreateFile` logs each not-found open at WARN | Issue and PR; our patch 28 |
| Case variants of one name in a host folder (D30) | A replace onto another spelling leaves the old file on case-sensitive hosts | Issue and PR; our patch 29 |
| `chrono_test` fails at the NT epoch on Linux (D29) | 1601 does not fit libstdc++'s nanosecond `system_clock` | Issue |
| `fctiw`/`fctid` round half away from zero on ARM64 (D22) | Still there | Issue and PR |
| `mffs` swaps round up and down on ARM64 (D23) | Still there | Issue and PR |
| `mtfsf` applies its field mask reversed (D24, all architectures) | Still there | Issue and PR |
| An unclaimed host fault hangs instead of crashing on POSIX (D25) | Still there | Issue and PR; our patch 30 |
| Wiki/code mismatches (D19) | The wiki documents the TOML key `enable_exception_handlers` (the code reads `generate_exception_handlers`) and describes `reserved_as_local` and `non_argument_as_local` wrongly | Small docs issue (found during this analysis) |

## Per patch

**1. vulkan-stencil-transfer.** In `VulkanRenderTargetCache`'s transfer shader, a stencil-only
transfer (`TransferOutput::kStencilBit`, the per-bit fallback used when the device has no shader
stencil export) never loads the source stencil into `packed`, so the per-bit discard never
discards and every pass writes its bit. Unchanged in `development`. Seen with NVIDIA, which has no
`VK_EXT_shader_stencil_export` on Linux, so the fallback runs; Mesa's Intel driver has the
extension and never takes this path (`vulkaninfo` on the test laptop: the GTX 1050 Ti lacks it,
the UHD 630 has it). Generic. Propose.

**2. shutdown-window-lifecycle.** `ReXApp::OnDestroy` removes its own listeners and resets the
window; when the guest ended the UI loop (`XamLoaderLaunchTitle` → terminate), the window was never
closed, so other listeners (input drivers) are not told and keep the pointer. One
`window_->RequestClose()` before the reset. `rex_app.cpp` changed upstream but `OnDestroy` is the
same. Generic. Propose.

**3. posix-wait-fraction.** `PosixConditionBase::WaitMultiple` truncates the remaining time to
whole milliseconds; below 1 ms it sleeps 0 and spins until the deadline. Unchanged. Generic on
POSIX. Propose.

**4. present-semaphore-lifetime.** `VulkanPresenter` has one `present_semaphore_` signalled by
every paint submission and awaited by every `vkQueuePresentKHR`; nothing proves the previous
present finished waiting on it before the next submit signals it again
(`VUID-vkQueueSubmit-pSignalSemaphores-00067`, device lost on NVIDIA). The patch keeps one
semaphore per swapchain image and retires presentations with present fences, which needs
`VK_EXT_swapchain_maintenance1`, and **fails presenter initialisation without it**. Upstream cannot
take that as is (older drivers, MoltenVK). Report the bug now; before a PR, add the fallback the
Vulkan guide recommends when the extension is missing (semaphore indexed by acquired image, without
present fences).

**5. vulkan-compute-write-mask.** `VulkanSharedMemory::GetUsageMasks` gives `kComputeWrite` only
`VK_ACCESS_SHADER_READ_BIT`. One flag. Propose.

**6. vulkan-texture-exponent-word.** The SPIR-V translator takes the result exponent bias from bits
13:18 of fetch constant word 4, which is `lod_bias`. `xenos.h` puts `exp_adjust` in dword 3 bit 13,
and the DXBC translator reads word 3 (`dxbc_translator_fetch.cpp`, "Apply the result exponent
bias", `RequestTextureFetchConstantWord(tfetch_index, 3)`). Generic. Propose.

**7. xam-exit-to-dashboard.** `XamLoaderLaunchTitle` with no path hits `assert_always` (aborts in
Debug, ignored in Release); exit to dashboard is a normal quit for a recompiled title. Propose.

**8. gpu-null-plugin.** A `rexgpu-null` plugin: the regular command processor with no draws and no
presenter. Generic and useful upstream, but the code is BelmanteGu's (Rayman Origins Recompiled,
via the `XDanfr/RaymanOriginsRecomp` fork), not ours. Keep it locally and rebase it on the next
update (the install rules moved to `rexglue_export_targets.cmake`). If it should go upstream, the
original author is the one to send it, or it goes with their agreement and credit.

**9. imgui-drawer-pending-dialogs.** `~ImGuiDrawer` does not delete open dialogs, so the fence that
`xeXamDispatchDialogEx` waits on is never signalled and `KernelState` teardown waits forever on the
dispatch thread. Unchanged. Generic. Propose.

**10. guest-vblank-rate.** Two things. (a) The vblank thread sleeps a fixed 1 ms between checks, so
each vblank arrives up to 1 ms late and 144 Hz comes out as 6/7/8 ms frames; the patch sleeps until
the next vblank (clamped to 0.1–1 ms). (b) `GraphicsSystem::SetGuestVblankRate(hz)`, which our
native mode uses as its frame cap. (a) is generic: propose. (b) is an API shaped by our use; keep it
local unless upstream wants it (mention it in the PR as optional).

**12. content-delete-backup.** Copies a content package to `save-backups/` before
`XamContentDelete` and friends remove it. A safety net for Torchlight's "corrupt save → delete
everything" dialog. It writes into the user's data root and sets a policy (and our retention lives
in the app); not an SDK bug. Keep locally.

**13. vfs-rename.** `Entry::Rename` takes `std::filesystem::path(...).filename()` of the guest
path, which on POSIX (where `\` is not a separator) is the whole path; the renamed entry can no
longer be found by name. `FILE_RENAME_INFORMATION.ReplaceIfExists` is also ignored. Unchanged.
Generic (any title that renames). Propose.

**14. vfs-delete-on-close.** `NtSetInformationFile(FileDispositionInformation)` stores the mark
(`Entry::SetForDeletion`) and nothing ever reads it, so `DeleteFile` does nothing. The patch
implements NT delete-on-close (last handle closes → delete, `STATUS_DELETE_PENDING` on reopen).
Depends on 13 (shared files). Generic. Propose after 13.

**15. shm-unlink-on-create.** Exactly issue #445 (open, 2026-09-20): `xenia_memory_*` stays in
`/dev/shm` after any abnormal exit, until later runs die with SIGBUS. Propose as the fix for #445.

**16. sdl-keystroke-repeat.** `SDLInputDriver::GetDeviceKeystroke` handles the repeat before looking at
button changes and never checks that the repeated button is still down: a title that stops
reading keystrokes for over 400 ms gets a repeat of a released button before its up. The upstream
input work since our base (`3cd7243`, `3f34ffc`) did not touch that logic. Propose.

**17. win-timer-resolution.** `rex::thread::Sleep` → `::Sleep` with whole milliseconds at the
default 15.625 ms timer. Propose (measurements in `patches/README.md`).

**18. win-sdl3-shared.** On Windows `rexruntime` is a DLL that does not re-export the static SDL3
it links, and the package passes `SDL3::SDL3-static` to the executable: two SDLs in one process,
and an executable calling SDL itself sees an uninitialised one. Affects every title that calls SDL
on Windows. Propose; the PR must say that `SDL3.dll` now ships next to the executable.

**19. mnk-keystrokes.** Upstream fixed the bug (#310, PR #311) and then reworked it in `3f34ffc`
(keystrokes for bound keys, diffed over the whole bind table, focus loss releases them, plus a
keyboard passthrough mode). Differences from ours: upstream has no repeat for bound keys (ours:
400 ms, then 100 ms while held) and does not use `keystroke_repeat.h`. Drop patch 19 on the next
base update and check in the game that the menus work with the keyboard; if hold-to-repeat is
missed, that is a small follow-up PR on top of 16, not this patch.

**20. vfs-wildcard-dos-semantics.** See the draft; the open point is that Win32 semantics are
documented and the console's are inferred.

## Other topics

**`RtlUnwind`.** Still `REXKRNL_WARN("[STUB] RtlUnwind called - not implemented")` in
`development`, with `__C_specific_handler` and `RtlCaptureContext` also stubs; no issue upstream.
Issue #409 is related but different (host C++ exceptions leaving `XThread`). The codegen's
`generate_exception_handlers` does not cover it: it wraps functions that have scope tables in
host `SEH_TRY`/`catch (...)` and calls their `__finally` handlers when a *host* exception passes;
a guest `RtlUnwind` called on a normal path (a local unwind, `return` out of a `__try`) never
reaches it. Report as an issue, with the local-unwind case as the concrete one; the fix is a design
choice for the maintainers.

**Codegen.** `docs/guest-hot-paths.md` asks for a report: registers stored to `PPCContext` on every
write and reloaded after every call; a compare writes all four CR bytes. The SDK already has the
switches for this, inherited from XenonRecomp and maintained (`f2b91f2` handles SEH funclets under
`non_volatile_as_local`): `cr_as_local`, `ctr_as_local`, `xer_as_local`, `reserved_as_local`,
`non_argument_as_local`, `non_volatile_as_local`, `skip_lr`, `skip_msr`, documented in the wiki
(`rexglue-CLI-Configuration-File`; two of its rows are wrong, D19). All default to false; when this was written `torchlight_manifest.toml` set none (since `b0b2bbb`
it sets four, see `docs/guest-hot-paths.md`), so the 903-for-176 measurement is the most conservative codegen. With CR fields as locals the
compiler drops the dead stores, which is the "only the bits that are read" request. Nothing to
report upstream until we have tried them. A local task, to plan separately: regenerate with
`cr_as_local`, `ctr_as_local`, `xer_as_local` first, then `non_volatile_as_local` (it also elides
`__savegprlr_N`/`__restgprlr_N`, the 4.6 % of the profile), measure, and check our hooks: any
hook that reads or writes `ctx.r14`–`r31`, CR or CTR of a guest function would stop seeing them.
`setjmp`/`longjmp` (not declared in our manifest yet) and `non_volatile_as_local` interact: the
generated `setjmp` saves and restores `ctx` only (`context.cpp`, `env = ctx` around
`ppc_setjmp`), so registers kept in locals and changed after the `setjmp` are indeterminate after a
host `longjmp`. That must be settled before turning it on.

**`-mcmodel=large`.** Forced in two places: the SDK's root `CMakeLists.txt`
(`add_compile_options(-mcmodel=large)` on Linux x86-64, for the SDK's own libraries) and
`rexglue_apply_target_settings` (every consumer target, plus `-Wl,--no-relax`). The comment says
"for linking with very large recompiled executables (35MB+)"; our executable has 57.5 MB of
`.text` (Release) and links with the small model. Measured here: +11 % main menu, +4.5 % dungeon
still, fight within noise (`docs/performance-profile.md`). Guest memory is a separate mapping, not
part of the image, so only the generated code and the SDK count against the small model's 2 GB. Propose an opt-out; the draft asks what failed at 35 MB.

**Wiki mismatch.** `rexglue-CLI-Configuration-File.md` lists the TOML key
`enable_exception_handlers`; `src/codegen/config.cpp` reads `generate_exception_handlers`
(`enable_exception_handlers` is only the CLI flag). A manifest following the wiki silently gets
nothing. The same table describes `reserved_as_local` as r1/r2/r13 (it is the `lwarx`
reservation) and `non_argument_as_local` as r11-r12 (it is r0, r2, r11, r12, f0, v32-v63).
Trivial issue (D19).

## Drafts

One issue and one PR per topic, against `development`. `#NNN` is the issue number once filed.
Patch files map one to one to the PR contents, except where a draft says otherwise; rebase each
on `development` and run clang-format before opening.

---

### D1. Vulkan: stencil-bit render target transfers write every bit

**Issue: `[Bug]: Vulkan stencil-only RT transfer sets every stencil bit (no shader stencil export)`**

When the device lacks `VK_EXT_shader_stencil_export`, `VulkanRenderTargetCache` copies stencil
through `TransferOutput::kStencilBit`: one pass per bit, each discarding the fragments whose
source bit is clear. For a stencil-to-stencil transfer the source stencil is never loaded into
`packed` (the color branch and the depth branch are skipped, the `kStencilBit` case falls through
both), so no fragment is discarded and the destination ends up `0xFF` whatever the source was.

Repro: any title whose EDRAM stencil gets transferred between render targets, on a device without
stencil export (NVIDIA on Linux: a GTX 1050 Ti does not expose it, Intel UHD 630 on Mesa does). In Torchlight the minimap reinterprets that EDRAM as color and
shows solid red. The same title on Mesa/Intel (which has the extension) is correct.

**PR: `fix(vulkan): feed the source stencil to stencil-bit transfers`**

Fixes #NNN. In the transfer shader builder, for `mode.output == TransferOutput::kStencilBit`,
use `source_stencil[0]` as `packed` before the depth branch, so the per-bit discard tests the
real source bits. Stencil-export devices do not use this path and are unaffected.

Tested: Torchlight on a GTX 1050 Ti (Linux, NVIDIA driver): the minimap is correct with the change.
The Intel UHD 630 (Mesa) never showed the bug, as expected.

---

### D2. Shutdown: input drivers keep a dangling window after a guest-initiated exit

**Issue: `[Bug]: Hang or mutex error on exit when the title quits by itself`**

When the title ends the run (e.g. `XamLoaderLaunchTitle` with no path → `TerminateTitle`), the UI
loop quits without the window ever being closed. `ReXApp::OnDestroy` removes its own listeners and
resets `window_`, but the other window listeners (the input drivers) are never notified and keep
the window pointer; their teardown then uses it. Result: a hang or a `std::mutex` error at exit.
Closing the window by hand does not trigger it, because that path notifies everyone.

**PR: `fix(ui): close the window before destroying it on a guest-initiated exit`**

Fixes #NNN. `ReXApp::OnDestroy` calls `window_->RequestClose()` after removing its own listeners
and before `window_.reset()`, so remaining listeners detach while the window is alive. A window
already closed by the user is unaffected.

Tested: Torchlight on Linux, quitting from its own menu: clean exit (before: a hang or a mutex
error at exit).

---

### D3. POSIX: timed waits spin for their last millisecond

**Issue: `[Bug]: POSIX WaitMultiple busy-polls when less than 1 ms remains`**

`PosixConditionBase::WaitMultiple` (`threading_posix.cpp`) sleeps
`min(duration_cast<milliseconds>(end_time - now), 1 ms)`. Once less than a millisecond is left,
the cast gives 0 and the loop spins with zero-length sleeps until the deadline. Guests that wait
in short slices (alertable 1 ms waits are common) keep a core busy.

**PR: `fix(threading): sleep until the deadline in POSIX timed waits`**

Fixes #NNN. Replace the truncated `sleep_for` with
`sleep_until(std::min(end_time, now + 1ms))`; the infinite-timeout branch is unchanged.

Tested: Torchlight on Linux, where the waits took a whole core before the change.

---

### D4. Vulkan presenter: present semaphore reused before its presentation finished (issue only)

**Issue: `[Bug]: VulkanPresenter signals present_semaphore_ again while a present may still wait on it`**

`VulkanPresenter` uses a single `present_semaphore_`: every paint submission signals it and every
`vkQueuePresentKHR` waits on it. Draw fences only cover the submit; nothing tells the presenter
that the presentation engine has consumed the wait, so the next submit can signal the semaphore
while it is still pending. The validation layers report
`VUID-vkQueueSubmit-pSignalSemaphores-00067`; on NVIDIA (Linux, GTX 1050 Ti) the game eventually
loses the device. The destructor's comment already notes the same lifetime question for
destruction.

The usual fix is one present-wait semaphore per swapchain image, chosen by the acquired image
index (the semaphore for image N is free again once image N is re-acquired), with
`VK_EXT_swapchain_maintenance1` present fences when available to know exactly when presentation
resources can be freed or recycled. We have a patch that does the extension path (and retires
presentations with those fences without a per-frame wait), but it requires the extension; before
offering it as a PR we would add the per-image fallback. Would you take that shape?

---

### D5. Vulkan: compute writes to shared memory declared as reads

**Issue: `[Bug]: VulkanSharedMemory kComputeWrite usage has no SHADER_WRITE access`**

`VulkanSharedMemory::GetUsageMasks` returns `VK_ACCESS_SHADER_READ_BIT` for
`Usage::kComputeWrite`. Barriers built from it do not make the compute shader's writes available,
so a later read may see stale data. Found on NVIDIA (Linux) while chasing device-stability
problems; the mask is wrong regardless of the driver.

**PR: `fix(vulkan): include SHADER_WRITE in the compute-write usage mask`**

Fixes #NNN. `access_mask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT` for
`kComputeWrite`.

---

### D6. SPIR-V: texture fetch exponent bias read from the wrong word

**Issue: `[Bug]: SPIR-V texture fetch applies exp_adjust from fetch constant word 4 (LOD bias)`**

`spirv_translator_fetch.cpp` applies the result exponent bias from bits 13:18 of fetch constant
word 4. Word 4 bits 12:21 are `lod_bias`; `exp_adjust` is word 3 bits 13:18 (`xenos.h`,
`xe_gpu_texture_fetch_t`), and the DXBC translator reads word 3
(`RequestTextureFetchConstantWord(tfetch_index, 3)` under "Apply the result exponent bias"). On
Vulkan, a texture fetched with a non-zero LOD bias gets its result scaled by a power of two taken
from the bias bits, and a real `exp_adjust` is ignored.

Repro: Torchlight's inventory/character screen renders transparent on Vulkan (NVIDIA, Linux).

**PR: `fix(spirv): take the result exponent bias from fetch constant word 3`**

Fixes #NNN. Load word 3 for the exponent bias; word 4 is still loaded for LOD biasing and stacked
filtering. Comment updated.

Tested: Torchlight inventory screen on NVIDIA/Linux renders correctly.

---

### D7. Debug builds abort when the title exits to the dashboard

**Issue: `[Bug]: XamLoaderLaunchTitle(NULL) aborts Debug builds`**

A title quitting asks for the dashboard with `XamLoaderLaunchTitle` and no path. That branch is
`assert_always("Game requested exit to dashboard ...")`, which aborts a Debug runtime; Release
ignores it and terminates the title. For a recompiled title the dashboard request is just a quit.

**PR: `fix(xam): treat exit to dashboard as a normal title exit`**

Fixes #NNN. Log at info level and fall through to the existing termination, as Release already
does. No other assert changes.

---

### D8. Hang on exit with a XAM dialog open

**Issue: `[Bug]: Exit hangs if a XAM dialog (keyboard, message box) is open`**

Quitting while a XAM dialog is showing hangs the process: the "Kernel Dispatch" thread waits in
`xeXamDispatchDialogEx` for the fence signalled when the dialog is deleted; `~ImGuiDrawer` does not
delete pending dialogs, and `KernelState` teardown waits for the dispatch thread forever.

Repro: open a message box or the virtual keyboard from the title and close the window.

**PR: `fix(ui): delete pending dialogs when the ImGui drawer is destroyed`**

Fixes #NNN. `~ImGuiDrawer` deletes the remaining dialogs (each removes itself from `dialogs_`),
signalling their fences. Their close callbacks do not run, so the pending XAM call completes with
an unspecified result; the title is already exiting.

---

### D9. vblank pacing: vblanks arrive up to 1 ms late

**Issue: `[Bug]: vblank thread polls every 1 ms, so high refresh rates come out uneven`**

The vblank thread in `GraphicsSystem::SetupGuestGpu` checks the time, fires due vblanks, then
sleeps a fixed 1 ms. Each vblank lands up to a poll late; at 144 Hz (6.94 ms) frames come out as
6, 7 and 8 ms. At 60 Hz the jitter is the same 1 ms, just less visible.

**PR: `fix(graphics): sleep until the next vblank in the vblank thread`**

Fixes #NNN. Sleep for the time left to the next vblank, clamped to 0.1–1 ms (so a cvar or mode
change still applies within a millisecond). The accumulated schedule is unchanged
(`last_frame_time += interval`), so 60 Hz vblanks fall on the same instants as before, only closer
to them. On Windows this depends on the timer resolution (D14) to be precise.

Optional, if useful to others: our patch also adds `GraphicsSystem::SetGuestVblankRate(hz)` so the
host can choose the guest's vblank rate while `vsync` is on (0 = the video mode's); we use it as a
frame cap. Happy to leave it out.

---

### D10. VFS: rename stores the whole guest path as the name on POSIX

**Issue: `[Bug]: Entry::Rename uses path::filename() on a guest path; ReplaceIfExists ignored`**

`Entry::Rename` takes the new name from `std::filesystem::path(guest_path).filename()`. On hosts
where `\` is not a separator that is the whole path (`SAVE:\1.TSV`), so the renamed entry cannot be
found by name afterwards. It also always stays in its old directory, and
`FILE_RENAME_INFORMATION.ReplaceIfExists` is never passed down: renaming onto an existing file
neither fails as NT does without the flag nor replaces as it does with it.

Repro: a title that saves safely by renaming (`N.TSV` → `backup.tmp`, `save.tmp` → `N.TSV`): from
the second save of a session the first rename fails.

**PR: `fix(filesystem): rename to the guest path's last component and honour ReplaceIfExists`**

Fixes #NNN. The new name is the last component of the guest path and the entry moves under the
destination directory's entry. `ReplaceIfExists` is passed through `XFile::Rename` and
`Entry::Rename`: without it an existing destination (also a host file the entry tree has not seen)
gives `STATUS_OBJECT_NAME_COLLISION` and nothing changes; with it the destination entry leaves the
tree first, so two entries never share a name. A missing destination directory gives
`STATUS_OBJECT_PATH_NOT_FOUND`. `MoveFileA` keeps its no-replace semantics.

Tests: `tests/unit/core/vfs_rename_test.cpp`. Also validated in Torchlight (repeated saves keep
`backup.tmp`).

---

### D11. VFS: DeleteFile does nothing (delete-on-close not implemented)

**Issue: `[Bug]: FileDispositionInformation marks the entry but nothing deletes it`**

Guests delete the NT way: `NtOpenFile` with `DELETE`, `NtSetInformationFile` with
`FileDispositionInformation`, `NtClose`. The SDK stores the mark (`Entry::SetForDeletion` from
`xboxkrnl_io_info.cpp`) and nothing ever acts on it, so the file stays. Any title that deletes
files is affected.

**PR: `feat(filesystem): NT delete-on-close for marked entries`** (after D10; shared files)

Fixes #NNN. Every open `File` registers on its entry; a marked entry is deleted when its last
open file closes (in `File`'s base destructor, after the derived file closed its host handle).
While marked and open, opening it again fails with `STATUS_DELETE_PENDING` (new
`X_STATUS_DELETE_PENDING`). An entry with open files is never destroyed by another path:
`Entry::Delete` fails and a rename does not replace an open destination
(`STATUS_ACCESS_DENIED`). Only an unmount with files still open destroys their entry; those files
then see `File::entry() == nullptr` instead of a dangling pointer.

Tests: `tests/unit/core/vfs_delete_on_close_test.cpp` (one and two handles, reopen while pending,
mark cleared, rename interplay, unmount with a file open). Validated in Torchlight: deleting a
character removes its files, saving keeps the previous one as `backup.tmp`, nothing else in the
container changes.

---

### D12. `/dev/shm` left full after abnormal exits (#445)

No new issue: #445 describes it.

**PR: `fix(memory): unlink the guest memory shm object right after creating it`**

Fixes #445. `CreateFileMappingHandle` unlinks the name as soon as the object is sized. Nothing
reopens it by name (every view maps the descriptor), and the kernel frees it when the last
descriptor and mapping go away, however the process ends. The `shm_unlink` in
`CloseFileMappingHandle` then finds no name and does nothing.

Tested: a run killed with SIGKILL leaves nothing in `/dev/shm` (during the run the mapping shows
as deleted in `/proc/<pid>/maps`); normal runs unchanged.

---

### D13. SDL input: keystroke repeat of a released button

**Issue: `[Bug]: SDL driver repeats a button already released when the title reads keystrokes late`**

`SDLInputDriver::GetDeviceKeystroke` processes the repeat (400 ms delay, then every 100 ms) before it
looks at button changes, and never checks that the repeating button is still pressed. If the title
does not read keystrokes for more than 400 ms after a press (a loading screen), its next read gets
a repeated *down* of a button that was released meanwhile, and only then the *up*. In Torchlight
that is a phantom A: the first character gets picked on entering "load character". XInput
only repeats while the button is held.

**PR: `fix(input): repeat SDL keystrokes only while the button is held`**

Fixes #NNN. Button changes are reported first; a button repeats only while held, and a released
one gets its up in the same read. The repeat state machine moves to
`include/rex/input/keystroke_repeat.h` (no SDL), which the MnK driver could share for bound-key
repeat later.

Tests: `tests/unit/input/keystroke_repeat_test.cpp` (delay and rate, release while nobody reads,
two buttons, ups before downs).

---

### D14. Windows: 15.6 ms sleeps

**Issue: `[Bug]: rex::thread::Sleep and short waits last up to 15.6 ms on Windows`**

`rex::thread::Sleep` calls `::Sleep` with whole milliseconds at the default 15.625 ms timer, so a
guest `Sleep(1)` and every short wait timeout can take 15.6 ms, and sub-millisecond waits become a
yield. Measured on the vblank thread at 120 Hz: median interval 14.9 ms, 254 of 597 vblanks within
0.5 ms of the previous one (bursts); at 60 Hz a p99 of 30.9 ms.

**PR: `fix(threading): 1 ms timer resolution and high-resolution waitable timers on Windows`**

Fixes #NNN. `timeBeginPeriod(1)` at static initialisation of `threading_win.cpp` (`timeEndPeriod`
at exit). `Sleep` and `AlertableSleep` wait on a per-thread waitable timer created with
`CREATE_WAITABLE_TIMER_HIGH_RESOLUTION` (Windows 10 1803+) for the requested microseconds, falling
back to `::Sleep`/`SleepEx` without it. After: vblank interval mean on target at 60/120/144 Hz,
median within 0.2 ms, p99 0.4–0.7 ms above, thread at 0–1 % of a core.

Tests: `tests/unit/core/threading_win_test.cpp` (Windows only; 5 of 6 cases fail without the
change).

---

### D15. Windows: two SDL3 instances in one process

**Issue: `[Bug]: Windows executables get their own static SDL3, separate from rexruntime's`**

SDL3 is built static on every platform. On Windows `rexruntime` is a DLL that exports only its own
symbols (`WINDOWS_EXPORT_ALL_SYMBOLS`), not the static libraries it links, and the package passes
`SDL3::SDL3-static` to every consumer. The process ends up with two SDLs: the runtime's (video, the
window, gamepads) and the executable's (never initialised). Any SDL call from the title's own code
(and from `windowed_app_main_sdl.cpp`, compiled into the executable) goes to the empty one: no
window, no displays, no gamepads from its point of view. On Linux the runtime's exported symbols
already give the process one SDL.

**PR: `fix(build): build SDL3 shared on Windows`**

Fixes #NNN. `SDL_SHARED` on, `SDL_STATIC` off under `if(WIN32)`; the package exports
`SDL3::SDL3-shared` and the runtime and executable share `SDL3.dll` (`SDL3d.dll` Debug,
`SDL3rd.dll` RelWithDebInfo). Other platforms unchanged. Packaging note: `SDL3.dll` must ship next
to the executable (it is staged with the other runtime DLLs).

Checked: the executable imports SDL from the DLL and no longer links `SETUPAPI`, `IMM32`,
`VERSION`; `unit_tests` pass as before.

---

### D16. VFS wildcard: `*.*` does not match names without a dot

**Issue: `[Bug]: Directory queries with "*.*" skip names without a dot (folders included)`**

`WildcardEngine` treats `*.*` literally: a name must contain a dot. Win32 `FindFirstFile` treats
`*.*` as "every name", and a trailing `.*` or `.` follows the DOS rules (`a*.*` matches `abc`,
`*.` matches only names without a dot). Titles written against those semantics list folders with
`*.*` and get no subfolders. In Torchlight the mod loader walks each mod folder recursively with
`*.*` and `<folder>/*.*`; `NtQueryDirectoryFile` receives the pattern unchanged, so no subfolder of
a mod is ever found.

Caveat we cannot settle without hardware: this is Win32 behaviour (where kernel32 rewrites `*.*`
before the kernel sees it); we infer the 360 matches it because the title's code expects it and
nothing between it and `NtQueryDirectoryFile` rewrites the pattern. If someone can list a folder
with `*.*` on a console, that would confirm it.

**PR: `fix(filesystem): DOS semantics for "*.*", "stem.*" and "stem." in WildcardEngine`**

Fixes #NNN. A pattern ending in `.*` also matches a name without a dot when its stem matches
(`*.*` matches everything); a pattern ending in `.` matches only names without a dot. Everything
else is unchanged (`*`, `*.dat`, `?`, exact names, a bare `.*` or `.`).

Tests: `tests/unit/core/filesystem_wildcard_test.cpp`.

---

### D17. `RtlUnwind` is a stub (issue only)

**Issue: `[Bug]: RtlUnwind is a stub, so __finally blocks are skipped on local unwinds`**

`RtlUnwind_entry` only logs `[STUB] RtlUnwind called - not implemented`; `__C_specific_handler`
and `RtlCaptureContext` are stubs too. Besides exceptions, MSVC's PPC code calls `RtlUnwind` on a
*normal* path: leaving a `__try` early (`return`, `goto`, `__leave` across scopes) is a local
unwind that runs the `__finally` handlers between the current point and the target. With the stub
those handlers are skipped and execution continues. (That our case takes this path is read from
the guest code around the call, not traced.)

Seen in Torchlight once per run at the main menu: a CRT `_wfopen` given an empty name returns NULL
correctly, but the `__finally` that unlocks the CRT stream slot it reserved is skipped, so that
`FILE` lock stays held by the thread (recursive, so the same thread is fine; another thread using
that stream would block). We also saw runs that ended in an endless loop of guest access
violations at a low address. That loop is D25 (an unclaimed fault is retried forever), and nothing
ties it to this stub.

`generate_exception_handlers` does not help here: it calls `__finally` handlers when a *host*
exception passes through the wrapper, while a local unwind is an ordinary call to `RtlUnwind`.

A possible shape for the local-unwind case: the codegen already parses each function's scope
table (`sehInfo`); it could emit a table from function to scopes, and `RtlUnwind(TargetFrame,
TargetIp, NULL, ...)` would find the function from the caller's `lr`, run the termination handlers
of the scopes that contain the call site and not `TargetIp`, innermost first, with the frame
pointer the handlers expect, and return. Full exception dispatch (`RtlRaiseException`, C++
`throw`) is a larger job and not what this issue asks. Is that direction acceptable, or do you
prefer to solve it in the codegen (emitting the handler calls at the call site)?

Whatever the shape, an unwinder or dispatcher that calls a funclet has to hand it the same state a
direct call does. The funclet reads its owner's frame pointer from r12 (see D26). Under
`non_volatile_as_local`, it also reads the owner's live r14-r31 from `ctx`, and those are host
locals of the owner. So the codegen has to provide what a direct call site already does: a copy of
the owner's localized r14-r31 into `ctx` before the funclet runs, and back after. Otherwise the
funclet reads stale values.

In Torchlight, 52 of the 92 `share_registers` functions are reachable only through exception
dispatch, so they never run today. The other 40 are called directly, and the SDK already copies
r14-r31 around those calls (checked call site by call site). None of the 92 reads a non-volatile
FPR or vector register of its owner, which matters because the copy covers GPRs only.

---

### D18. `-mcmodel=large` forced on Linux x86-64

**Issue: `[Feature]: Let consumers build with the default code model on Linux x86-64`**

The SDK compiles its own libraries (`add_compile_options(-mcmodel=large)` in the root
`CMakeLists.txt`) and every consumer target (`rexglue_apply_target_settings`: `-mcmodel=large`,
`-Wl,--no-relax`) with the large code model on Linux x86-64. Every call between generated functions
becomes a `movabs` and an indirect call. Windows and macOS already use the default model.

Measured on Torchlight (57.5 MB of `.text` in Release; 67 MB in RelWithDebInfo), overriding with
`-mcmodel=small` on the game targets only, SDK libraries unchanged: one generated file has 5.6 %
fewer instructions (52,811 vs 55,955) and 2,054 of its 2,114 calls become direct; whole game, two
runs each, interleaved: main menu 269 → 299 fps (+11 %), dungeon idle 157 → 164 fps (+4.5 %),
combat within noise. It links and runs normally; mixing code models across the SDK libraries is
fine.

The CMake comment cites "very large recompiled executables (35MB+)". Do you remember what failed
there? A plain 35 MB text is far from the small model's 2 GB, so it may have been a specific
relocation (e.g. a large static table or `--relax` rewriting), which would decide whether an
automatic default is safe.

**PR: `build: make the large code model optional on Linux x86-64`**

Fixes #NNN. An option `REXGLUE_LARGE_CODE_MODEL` (proposed default `OFF`; or `ON` to keep today's
behaviour if you prefer to be conservative) that controls both places. `-Wl,--no-relax` goes with
the large model only. We have only measured with `--no-relax` kept; that part needs a link check
on a large title.

---

### D19. Wiki: codegen option keys and scopes that do not match the code

**Issue: `[Docs]: Configuration-file page: enable_exception_handlers is not the TOML key; two *_as_local rows describe other registers`**

`rexglue-CLI-Configuration-File` documents the TOML key `enable_exception_handlers`.
`src/codegen/config.cpp` reads `generate_exception_handlers`; `enable_exception_handlers` is only
the CLI flag (`--enable_exception_handlers`). A manifest that follows the wiki gets no SEH wrappers
and no warning. Either fix the wiki or accept both keys.

Two rows of the same table do not match `BuilderContext` (`src/codegen/builders/context.cpp`):

- `reserved_as_local` says "reserved registers (r1, r2, r13)". It makes the `lwarx`/`stwcx.`
  reservation (`ctx.reserved`) a local; r1, r2 and r13 stay in `ctx` with every flag.
- `non_argument_as_local` says "r11-r12". It covers r0, r2, r11, r12, f0 and v32-v63.

The define table in `Generated-Code-Structure` lists internal field names
(`ctr_as_local_variable`, ...) as the config flags, not the TOML keys, which is also confusing.

---

### D20. Tests: `ppc_tests` and `unit_tests` do not build on ARM64 or macOS

**Issue: `[Build]: SDK tests need x86 flags and the bundled PowerPC binutils`**

Three things stop `REXGLUE_BUILD_TESTS=ON` outside Linux and Windows x86-64:

- `tests/ppc/CMakeLists.txt` passes `-msse4.1 -mssse3` to `ppc_tests` unconditionally. The root
  `CMakeLists.txt` and `rexglue_apply_target_settings` already guard `-msse4.1` with
  `CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64"`.
- `cmake/ppc_test_pipeline.cmake` needs `tools/binutils/powerpc-none-elf-*`, shipped for Linux and
  Windows only, and stops the configure without them (macOS).
- `tests/unit/codegen/codegen_writer_test.cpp:128` does
  `CHECK(fs::last_write_time(probe) == before)`. Catch2 then has to stringify a
  `std::filesystem::file_time_type`, which does not compile with Apple's libc++.

**PR: `build: let the SDK tests build on ARM64 and without the PowerPC binutils`**

Fixes #NNN.

- Guard the SSE flags of `ppc_tests` like the other targets.
- Add a `REXGLUE_PPC_TEST_BIN_DIR` cache path. When it is set, `ppc_tests` takes the `.bin` and
  `.map` files from there instead of assembling `tests/ppc/asm`; a missing file is a configure error.
  Empty (the default) keeps today's pipeline.
- Compare the file times into a `bool` and `CHECK` that.

Tested on Linux x86-64: `ppc_tests` passes either way (1462 cases), and the prebuilt files are byte
identical to the ones the pipeline assembles. Built and run on macOS ARM64 by <confirm with the
macOS port before sending>.

---

### D21. Codegen: `non_volatile_as_local` and `setjmp`/`longjmp`

**Issue: `[Codegen]: non_volatile_as_local leaves r14-r31 indeterminate after a longjmp`**

With `non_volatile_as_local`, a function keeps r14-r31 in host locals. The generated `setjmp`
saves and restores `ctx` only (`env = ctx; ppc_setjmp; if (temp) ctx = env;`), so after a
`longjmp` back into it those locals hold whatever they had when the jump left, not their values at
the `setjmp`. A title that uses `setjmp`/`longjmp` cannot turn the flag on safely.
`share_registers` does not fit as a per-function opt-out, because its copy-back assumes a funclet.

Possible directions:
- Spill the localized registers into `ctx` before `ppc_setjmp` and reload them after it returns,
  both times.
- A per-function option to keep the registers in `ctx`, without the copy-back.

(For us this is latent: our only `setjmp` users are on image-decoder error paths, never seen to
run. We have not enabled the flag.)

---
### D26. Codegen: `non_argument_as_local` drops values passed in r12 (and r11) outside the ABI

From the render work's step E (2026-10-08). The codegen rules were checked here on `bd833a2`; the
deadlock was seen there on `0c7b01a`.

**Issue: `[Codegen]: non_argument_as_local breaks SEH funclets and stack probes that take r12`**

`non_argument_as_local` makes r0, r2, r11 and r12 C++ locals in every function
(`BuilderContext::r`, `src/codegen/builders/context.cpp`). That assumes no callee reads them on
entry. MSVC's Xbox 360 code breaks that assumption in two places:

- **`__try`/`__finally` funclets get their owner's frame in r12.** The owner sets r12 right before
  the `bl`, and the funclet starts with `addi r31,r12,-N`. Example from a title, generated:

      // addi r31,r12,-112
      ctx.r31.s64 = ctx.r12.s64 + -112;

  With the flag, r12 is a local in the owner and in the funclet. The funclet reads its own
  uninitialised local and computes a wrong frame. `share_registers` does not help: it exempts only
  r14-r31 from localization, and the copy through `ctx` around the call (`ctx.r{i} = r{i}` before,
  back after) covers only those registers.
- **The stack probe (`__chkstk`-like) takes the allocation size in r12**:
  `neg r11,r12` at entry. Some other functions also read r11 set by their caller.

Seen in Torchlight: 88 functions start with `r31 = r12 - N`. With `reserved`, `xer`, `ctr`, `cr`,
`non_volatile` and `non_argument_as_local` on, the game deadlocks about 3 s after start:
- three guest threads wait in `RtlEnterCriticalSection`, entered from the CRT heap functions;
- the funclets that should run `_unlock` in their `__finally` read a wrong frame;
- the same build without `non_argument_as_local` starts.

**Possible fixes** (any one):
- Do not localize r11/r12 in functions that call a `share_registers` function, nor in the funclets
  themselves; and copy them through `ctx` around such calls, as is done for r14-r31.
- Treat r11 and r12 as live-in for any callee that reads them before writing them. The function
  graph can see that from the callee's first instructions. It covers the stack probe and the
  caller-set r11 cases too.
- At least, document that the flag is unsafe for MSVC code with SEH or large stack frames.

The non-volatile save and restore helpers are already elided under `non_volatile_as_local`, so they
are not affected.

We keep `non_argument_as_local` off.

---

### D27. Guest file flushes never reach the disk

From the render work (2026-10-08), checked here on `bd833a2`. The macOS port asked about
`F_FULLFSYNC` for saves, which is what turned this up.

**Issue: `[Kernel]: saves are never flushed: XamContentClose, XamContentFlush, NtFlushBuffersFile and FlushFileBuffers do not write through`**

A title that saves trusts that a closed (or flushed) content package is on the disk. Today it is
only in the host's cache:

- `XamContentClose_entry` (`src/kernel/xam/xam_content.cpp`) unmounts the package and nothing else.
- `XamContentFlush_entry` completes the overlapped with success.
- `NtFlushBuffersFile_entry` (`src/kernel/xboxkrnl/xboxkrnl_io.cpp`) fills the status block with
  success.
- The CRT's `FlushFileBuffers_entry` (`src/kernel/crt/file.cpp`) returns 1.

`FileHandle::Flush` (`fsync`, `FlushFileBuffers`) exists, but no guest path calls it. A crash or a
power loss right after a save can lose it.

What a real title does, measured with Torchlight (a guided save under `strace`, with a first
version that flushed only the handles still open):
- It writes its save files and closes them, then calls `XamContentFlush` and `XamContentClose`.
  By then nothing is open, so flushing open handles does nothing: the `strace` showed no `fsync`
  at all.
- Its one `NtFlushBuffersFile` call site is not on the save path.
- The save replaces the character file by name (`4.tsv` before, `4.TSV` after). So the rename and
  the removal must be durable too, which means the folder.

So the commit point is the content close, as on the console, and it has to cover files written
through handles closed long before.

**PR: `fix(kernel): closing or flushing a content package writes it through to the disk`**

Fixes #NNN.
- A content package's `HostPathDevice` tracks, from mount, the host files written, created or
  renamed into place, and the folders whose entries changed. Tracking is opt-in per device, so
  game data and other writable devices are unaffected.
- `XamContentFlush` and `XamContentClose` (before the unmount) flush each of those files, reopening
  it by path, then each folder, and forget them. A renamed file or folder carries what is
  remembered under it to the new path.
- A failed flush is the result of `XamContentFlush` and of `XamContentClose`, which unmounts
  either way, and is logged at WARN. On the console the close is the commit, so the title should
  hear of it. Checked first that this cannot cost a player their saves in Torchlight: the game
  never reads its `XamContentClose` result, and its one `XamContentFlush` is in Microsoft's
  telemetry library, which only logs the failure. Its "Corrupt/Damaged Save" dialog, whose "Yes"
  deletes the whole container, comes from short reads. A title that answers a failed close by
  discarding its save would need the failure kept in the log only.
- `NtFlushBuffersFile` and `FlushFileBuffers` flush their handle's file. On a handle that is not a
  file they now fail (`X_STATUS_INVALID_HANDLE`, 0) instead of succeeding.
- `FileHandle::Flush` returns its result. A new `FlushFolder` does the same for a folder: `fsync`,
  with `F_FULLFSYNC` first on Apple because `fsync` there stops at the drive's cache. On Windows,
  NTFS journals folder entries.

The other design, flushing every written file on close, does not depend on the title closing its
content. But it would force the disk for every temporary file of every title, which the console
does not do.

Test: VFS unit tests with a counting `FileHandle` and a real host folder. The case Torchlight hits
is in them: a temporary file written, the old one removed, the temporary renamed into place, one
file and one folder flushed. Another writes a file in a folder, renames the folder, and expects
the file flushed under its new path.

Cost, measured in a Torchlight save-and-exit under `strace`:
- The `XamContentFlush` flushed one file: 68 ms.
- The `XamContentClose` flushed two files and the folder: 69 ms, 0.04 ms and 1.2 ms.
- Total: about 140 ms on the game thread per save-and-exit.

On macOS ARM64 (APFS) our port saved and loaded a character twice in the game with patch 27
(2026-10-09, `docs/macos-port.md` on `docs/macos-port-plan`, 2078e54). Each Exit to Title logged
`flushed 1 files and 0 folders`, then `flushed 2 files and 1 folders` 5 s later. The save file and
the shared stash were rewritten, and the next start loaded the character where it was saved. The
`F_FULLFSYNC` calls were not timed in the game (no `fs_usage`); on that volume
`fcntl(F_FULLFSYNC)` took 3-10 ms for a 200 KB file in a separate program.

On ext4 an `fsync` waits for the journal commit, so its time depends on what else is dirty on the
system, not on the file. A 112 KB file took 1.9 ms with nothing else dirty, 11-36 ms with
16-256 MB of other dirty data pending, and 85 ms as the first file of a new folder.

The flushes stay synchronous, as `XamContentClose` is on the console, so a flush error can reach
the title. Where Torchlight commits, from a guided run with patch 27's log line (2026-10-09):
- At each zone change, under the loading screen: two files right before the level load and two
  right after.
- In the options menu, two files each time, with the menu open.
- At save-and-exit.
- Never from the shared stash, the inventory, a vendor, fighting or walking.

The user's criterion: a synchronous commit is fine with a menu or a loading screen open. Only a
commit in open play (fighting or walking) would need the flushes moved to a worker thread first.

Not verified yet: the event that runs when Alric's quest completes, after the final boss
(`0x823CEED0`), closes a content package and may write. Whoever has a save that far along should
play it through with patch 27 and check whether a `Content ...: flushed` line appears in open play.

Our patch 27 (`patches/README.md`), approved by the user on 2026-10-08.

---

### D28. Kernel: every open of a missing file is logged as a warning

Found with a PC mod pack in Torchlight (2026-10-09). Checked on `bd833a2`.

**Issue: `[Kernel]: NtCreateFile logs "file not found" at WARN, which floods the log when a title probes for files`**

`NtCreateFile_entry` (`src/kernel/xboxkrnl/xboxkrnl_io.cpp`) logs every failed open with
`REXKRNL_IMPORT_FAIL`, which is WARN. A missing path is an ordinary result: titles probe for
optional files, and a title with mod or patch folders looks each data file up in every folder.
With a 29-mod pack, Torchlight wrote about 124,000 such lines, 20 MB, in the first 45 s of
startup. They also hide the failures that matter.

**PR: `fix(kernel): log NtCreateFile's not-found results at debug`**

Fixes #NNN. `X_STATUS_NO_SUCH_FILE`, `X_STATUS_OBJECT_NAME_NOT_FOUND` and
`X_STATUS_OBJECT_PATH_NOT_FOUND` go to DEBUG. Every other failure stays at WARN. `NtOpenFile`
shares the code. Our patch 28 (`patches/README.md`). With it, the same 29-mod startup wrote
1.13 MB of log in the first 45 s instead of 14.5 MB, and no not-found line at INFO (2026-10-09).

---

### D29. Tests: `chrono_test` fails at the NT epoch on Linux

Seen while validating the series (2026-10-09). The render agent reproduced it on a clean `bd833a2`
with no patches, so it is not ours.

**Issue: `[Tests]: chrono_test fails for 1601-01-01 on Linux: the NT epoch is outside system_clock's range`**

On Linux x86-64 (clang, libstdc++, Release), `unit_tests` fails 4 cases of
`tests/unit/core/chrono_test.cpp`, 11 assertions, all at the NT epoch (FILETIME 0, 1601-01-01):
- "from_sys then to_sys round-trips for whole-second values" (line 109);
- "calendar decomposition: NT epoch (1601-01-01)" (lines 150-157): it gives 2185-07-21
  23:34:33.709, a Thursday;
- "calendar recomposition: known dates produce correct FILETIMEs" (line 240):
  `recompose(1601, 1, 1, ...)` gives 184467440737095516;
- "calendar recomposition: decompose then recompose round-trips" (line 253).

The other dates (1970, 2000, 2000-02-29, 2020, 2021) pass. The wrong values match this cause.
`WinSystemClock::to_sys` (`include/rex/chrono/chrono.h`) converts to
`std::chrono::system_clock::duration`, which in libstdc++ is a signed 64-bit count of nanoseconds.
That covers about 1677 to 2262, so 1601 overflows:
- 1601 plus 2^64 ns (584.55 years) is mid-2185, the date the decomposition gives;
- 184467440737095516 is 2^64 / 100, the same wrap counted in FILETIME's 100 ns units.

MSVC's `system_clock` counts 100 ns and libc++'s counts microseconds. Both reach 1601. On macOS
ARM64 (Apple clang, libc++) `chrono_test` passes, the NT epoch included (our macOS port,
2026-10-09, `docs/macos-port.md` on `docs/macos-port-plan`), so the failure is Linux/libstdc++
only. Windows should pass too; not run there yet. The test came in with `952828d`
(2026-02-19), the same day as `4c981fe` ("replace date:: with std::chrono::"). It was not built at
those commits, so "since then" is a reading.

Fix options for upstream: keep the 1601 cases out of the `system_clock` round trips on
libstdc++, or convert through a `sys_time` with a coarser duration (seconds or 100 ns) instead of
`system_clock::duration`. Nothing in the runtime is known to pass FILETIMEs that old to `to_sys`.

---

### D30. Filesystem: names that differ only in case in a host folder

Asked for by the owner after a reading of the code by the render agent (2026-10-09). Checked on
`bd833a2` with our series.

**Issue: `[Filesystem]: a rename that replaces a case variant leaves the old file on case-sensitive hosts`**

Host folders mounted by `HostPathDevice` can hold names that differ only in case (`0.tsv` and
`0.TSV`), for example a save copied by hand. The guest sees one name. `PopulateEntry` adds both,
`Entry::GetChild` returns the first match in the host's listing order, and an enumeration lists
both, so a title lists one save twice and never opens the other.

`Entry::Rename` with `replace_existing` takes `GetChild(new_name)` as the replaced entry. When its
spelling differs from `new_name`, `HostPathEntry::RenameEntryInternal` renames to the new
spelling, which on a case-sensitive host creates a second file. `Rename` then drops the replaced
entry from the tree as if the host had replaced it. The old file stays on disk and is back at the
next mount.

**PR: `fix(filesystem): a replacing rename removes the destination's case variants`**

Fixes #NNN. `Rename` collects every sibling that matches the new name ignoring case (each must
still be a file with no open handle). `RenameEntryInternal` gets them and, after the host rename
succeeded, removes the host file of each one that is not `std::filesystem::equivalent` to the
destination. On a case-insensitive host (Windows, macOS's default APFS) the variant is the file
just replaced, now holding the new data, so nothing is removed there; an error from `equivalent`
also keeps the file. The mount logs a warning per group of case variants. Tests: a rename onto a
case variant (every host), the same with the case-insensitive host simulated on Linux (the
replaced spelling made a symbolic link to the destination after the mount), and two variants at
mount. Our patch 29 (`patches/README.md`).

---

### D22. Codegen: `fctiw`/`fctid` ignore the rounding mode on ARM64

The macOS port found it by reading the code (`docs/macos-port.md` on `docs/macos-port-plan`,
section 1, item 2). Checked here on `bd833a2`.

**Issue: `[Codegen]: fctiw/fctid round half away from zero on ARM64`**

`build_fctiw` and `build_fctid` (`src/codegen/builders/floating_point.cpp`) emit
`simde_mm_cvtsd_si32` / `simde_mm_cvtsd_si64`. On x86-64 these are `cvtsd2si`, which rounds by
MXCSR, and MXCSR follows the guest's FPSCR[RN]. On ARM64 SIMDe has no native path for them
(`thirdparty/simde/simde/x86/sse2.h`, `simde_mm_cvtsd_si32` and `_si64`). It calls
`simde_math_round`, which is C `round`: half away from zero, whatever the guest's mode.

Reproduced on x86-64 with SIMDe's portable path (`-DSIMDE_NO_NATIVE`, the code ARM64 runs), under
each `fesetround` mode:

| Input | Mode | Native (`cvtsd2si`) | Portable (SIMDe) | PowerPC |
|---|---|---|---|---|
| 2.5 | nearest | 2 | 3 | 2 |
| -2.5 | nearest | -2 | -3 | -2 |
| 2.7 | toward zero | 2 | 3 | 2 |
| 2.5 | down | 2 | 3 | 2 |
| -2.5 | up | -2 | -3 | -2 |

`fctiwz`/`fctidz` (`cvttsd2si`, truncation) are right on both. `tests/ppc` only covers `fctiwz`, so
`ppc_tests` passes on ARM64 with this bug.

A second, smaller bug, found in review: `fctid` and `fctidz` saturate with
`> double(LLONG_MAX)`, and `double(LLONG_MAX)` rounds to 2^63. So 2^63 itself goes to the
conversion, which is out of range. On x86-64 `cvtsd2si`/`cvttsd2si` give 0x8000000000000000
(INT64_MIN) instead of INT64_MAX. On ARM64 `fcvtzs` saturates to INT64_MAX by itself, so the
result happens to be right there, but the check is still wrong and the out-of-range conversion is
undefined in C++. `fctiw` and `fctiwz` use `>=` with `INT_MAX`, which is exact in a double, and
are right.

Run on ARM64 by our macOS port (2026-10-09, `docs/macos-port.md` on `docs/macos-port-plan`),
with patch 23's tests:
- without the fix, 12 of the 20 `fctix_rounding` cases fail: the rounding cases (2.5 and -2.5 to
  nearest, 2.7 and -2.7 toward zero, -2.5 up, 2.5 down), each for `fctiw` and `fctid`. The two
  2^63 cases pass, for the reason above. On x86-64, 2 of 20 fail: only the 2^63 cases;
- with the fix, `ppc_tests` passes 1492 of 1492.

**PR: `fix(codegen): fctiw/fctid round in the current rounding mode on every architecture`**

(Our patch 23, with the tests below; `patches/README.md`.)

Fixes #NNN. Convert with `std::nearbyint` (or `llrint`), which honours the FP environment that
`storeFromGuest` sets on both architectures, and keep today's NaN and saturation handling. The
generated code must not let the compiler fold the conversion across an `mtfsf`, so either keep it
behind a call or compile with `-frounding-math`. Saturate `fctid` and `fctidz` with `>=`. Tests:
`fctiw` and `fctid` of 2.5, -2.5, 3.5 and 2.7 under each of the four modes, set with `mtfsf 0xFF`,
and `fctid` and `fctidz` of 2^63 (the two fail without the fix on x86-64; on ARM64 they pass either
way).

---

### D23. Runtime: `mffs` reports round up and round down swapped on ARM64

Also from the macOS port (item 3). Checked here by reading the code only; not run on ARM64 yet.

**Issue: `[Runtime]: FPSCRRegister::HostToGuest uses the MXCSR order on ARM64`**

`FPSCRRegister::HostToGuest` (`include/rex/ppc/context.h`) is
`{kRoundNearest, kRoundDown, kRoundUp, kRoundTowardZero}`, indexed by the host's rounding field.
That is MXCSR.RC's order (00 nearest, 01 down, 10 up, 11 zero). ARM64's FPCR.RMode is 00 nearest,
01 up (RP), 10 down (RM), 11 zero. `FPSCRPlatform::GuestToHost` (`include/rex/platform/fpscr.h`)
already has the ARM64 order, so the write path is right and the read path is wrong.

On ARM64:
- `mffs` (`loadFromHost`) reports up as down and down as up.
- A partial `mtfsf` reads the mode back through the same table (`build_mtfsf`, the
  `loadFromHost() & ~mask` branch), so writing other FPSCR fields flips an up or down mode.
- The common save, change and restore sequence (`mffs`, `mtfsf`, then `mtfsf` of the saved value)
  restores the opposite mode.

**PR: `fix(runtime): read the ARM64 rounding mode back in FPCR order`**

(Our patch 24, with the tests below; `patches/README.md`.)

Fixes #NNN. Move `HostToGuest` into `FPSCRPlatform`, next to `GuestToHost`, with the ARM64 order
`{kRoundNearest, kRoundUp, kRoundDown, kRoundTowardZero}`. Test: for each mode, `mtfsf 0xFF` and
then `mffs` returns it.

---

### D24. Codegen: `mtfsf` applies its field mask reversed

Found here while checking D23, on every architecture.

**Issue: `[Codegen]: mtfsf with a partial FM writes the wrong FPSCR fields`**

`build_mtfsf` (`src/codegen/builders/system.cpp`) builds the mask with
`if (fm & (1 << (7 - j))) mask |= 0xF << (4 * j)`. `FM[0]`, the most significant bit of the
8-bit field, selects FPSCR field 0, which is bits 0-3 in PowerPC numbering (`0xF0000000`). The
code maps it to `0x0000000F`, the field that holds RN. So `mtfsf 1,f1` encodes FM = 0x01
(`fc 02 0d 8e`), which should write field 7 (the rounding mode), but generates

    ctx.fpscr.storeFromGuest((ctx.fpscr.loadFromHost() & 0x0FFFFFFF) | (ctx.f1.u32 & 0xF0000000));

and the mode does not change. Checked on x86-64 with a PPC test, `mtfsf 1,f1` with RN = 3 in `f1`
followed by `mffs f2`: it gives 0, expected 3. `mtfsf 0xFF` (the full mask) is right.

**PR: `fix(codegen): mtfsf field mask in PowerPC bit order`**

(Our patch 25, with the tests below; `patches/README.md`.)

Fixes #NNN. `if (fm & (0x80 >> j)) mask |= 0xF0000000u >> (4 * j)`. Test: `mtfsf 1` sets RN and
`mtfsf 0x80` leaves it alone, each followed by `mffs`. (`mtfsfi`, `mtfsb0` and `mtfsb1` have no
builder: they reach the unimplemented-instruction trap.)

---

### D25. POSIX: a fault no handler claims hangs instead of crashing

From the macOS port (item 4). Reproduced here on Linux x86-64.

**Issue: `[Runtime]: unhandled SIGSEGV re-faults forever on POSIX`**

`ExceptionHandlerCallback` (`src/core/exception_handler_posix.cpp`) tries the installed handlers
and returns when none claims the fault. It never chains to the handler it replaced
(`original_sigsegv_handler_` and the others) or to the default action, so the faulting instruction
runs again and faults again, forever: a host crash becomes a hang at 100 % of a core, with no
crash report. The same happens with the early `return` on SIGBUS outside macOS
(`assert_unhandled_case` does nothing in Release). It matters more on macOS, where its page
handling can leave pages inaccessible that are accessible on Linux.

This is also what happens to a guest access the guest memory cannot serve. In
`Memory::AccessViolationCallback` (`src/system/xmemory.cpp`), an address outside the physical heaps
logs `Unhandled guest access violation: read of guest 0x...` and returns `false`; then the
callback above returns, the access runs again, and it logs again. Seen in Torchlight (a mod
validation run, a guest read near address 0): 14 rotated 5 MB logs within seconds and a process
that only `kill -9` ended. On a console that read is a crash.

Reproduction against the installed SDK: in a forked child, install a handler that returns `false`,
set `alarm(3)` and read address 16. The child is killed by SIGALRM after 3 s. It should be killed
by SIGSEGV at once.

**PR: `fix(core): chain unclaimed faults to the previous handler on POSIX`**

Fixes #NNN. When no handler claims a fault, call the handler the SDK replaced
(`original_sigsegv_handler_` and the others): its `sa_sigaction` with the same arguments, or its
`sa_handler`. If that was `SIG_DFL`, reset the signal to the default and return, so the
re-executed instruction terminates the process. This keeps the SDK's handler installed. That
matters: restoring the original `sigaction` instead would uninstall it, and the next MMIO access or
write watch would go unhandled. A previous handler can still decide to retry by returning, or end
the process; a crash reporter installed before the SDK sees every fault the SDK does not claim.
Test: the child above dies by SIGSEGV; with a previous handler that counts calls, that handler is
called once per unclaimed fault and an MMIO access still works.

Our patch 30 (`patches/README.md`, 2026-10-10). Its tests run each case as a hidden case of
`unit_tests` in a new process, since the test memory installs the SDK's handler in the test
process: an unclaimed read ends by SIGSEGV, a previous handler gets the fault after the SDK's
handlers saw it once, and a handler that unprotects the page still lets the write through.

---

Not drafted, from the same port (`docs/macos-port.md`, section 1, items 5-7, and section 2):
- No `SO_NOSIGPIPE` / `MSG_NOSIGNAL` in `xsocket.cpp`.
- Scalar audio fallbacks, one of which truncates where x86 rounds.
- A partial ARM64 MMIO decoder.
- The `mac-arm64` presets' `-march=armv8-a`, which may keep LSE atomics out of every
  compare-exchange.

They are lower risk for this game or still need a measurement. Each becomes a draft when it has
one.

---

## Updating our base from `0c7b01a`

Decided (2026-10-08): update to `development`, as its own task, **after** the codegen options
(`cr_as_local` and the rest, above) are measured on the current base, so one change does not hide
the other. The drafts above are sent only after they are rebased on `development`.

### What upstream brings (19 commits, to `bd833a2`)

- *Codegen* (regenerates `generated/`): `stwcx.`/`stdcx.` as `std::atomic`
  `compare_exchange_strong` (acq_rel) instead of `__sync_bool_compare_and_swap`, `CR0.so` dropped,
  register fields unsigned (`7f7c92e`, `ea222e9`); `sync` and `eieio` emit a `seq_cst` fence
  (`mfence` on x86-64) and `lwsync` an `acq_rel` fence (no instruction on x86-64, only a compiler
  barrier) (`bd833a2`); `vpkuwus`/`vpkuhus` aliasing (`6319e23`). Torchlight's generated code has
  185 `stwcx.`, 15 `sync`, 18 `eieio`, 42 `lwsync`, no `vpkuwus`/`vpkuhus`.
- *Input:* vibration, deadzones, hotplug notices, real XInput subtypes, guest input blocked while
  a XAM dialog is shown (`3cd7243`, `ScopedGuestInputBlock` in `xam_ui.cpp`); MnK keystrokes for
  bound keys and keyboard passthrough (`3f34ffc`, supersedes patch 19).
- *UI:* window size cvars applied live, display mode switch in fullscreen, input off while the
  window has no focus (`1406e1b`, `923c1a5`, `289f518`; the focus check lives in `ReXApp`'s active
  callback).
- *Logging:* `LogConfig` carries the log path, directory budget and flush (`b971840`);
  `ApplyLogCvarOverrides` takes `log_file` when it has a non-default value.
- *System/build:* the `writable_executable_memory` cvar is replaced by `writable_code_segments`
  (default false: code segments read-only, as before; nothing here sets either), SSE4.1 required
  on SDK targets, imgui and xxHash headers public (`b5e0cf8`), a version resource on Windows
  binaries, the export rules moved to `cmake/rexglue_export_targets.cmake`.

No vendored dependency changed (no `thirdparty/` path in the diff).

### 1. The rebased series (prepared, branch `sdk/rexglue-next`)

Checked with `git apply` in order on `bd833a2`; not built yet.

- `tools/deps/build_sdk.sh`: `SDK_COMMIT=bd833a2`. `tools/deps/key.sh` changes the dependency key by
  itself (it hashes the script, `series` and the patches), on Linux and Windows.
- Patch 8 (`rexglue-gpu-null-plugin.patch`) regenerated: `rexgpu-null` goes into
  `REXGLUE_INSTALL_TARGETS` in `cmake/rexglue_export_targets.cmake` (the list left
  `rexglue_install.cmake`), and gets `rexglue_add_version_resource` like `rexgpu-xenos`. The rest
  of the patch (sources, `null_gpu.cpp`) is unchanged.
- Patch 19 (`rexglue-mnk-keystrokes.patch`) removed from `series` and from the tree; its README
  entry says why, as patch 11's does.
- Patches 1-7, 9, 10, 12-18 unchanged (byte for byte). Patch 20 (wildcard, `feature/pc-mods`)
  applies on top of the rebased series as is; it joins `series` when that branch is merged.
- `patches/README.md`, `THIRD_PARTY_NOTICES.md`, `docs/release-pipeline.md`:
  the base commit. `torchlight_manifest.toml`'s first line is regenerated by the codegen.

If `development` moves before the update, the same check is repeated on its new tip and the pin
moves with it.

### 2. Build the new SDK apart (`~/rexglue-sdk-next`)

```sh
tools/deps/build_sdk.sh ~/rexglue-sdk-next/install ~/rexglue-sdk-next all
```

from a worktree of `sdk/rexglue-next`: the checkout stays in `~/rexglue-sdk-next/rexglue-sdk`, the
install in `~/rexglue-sdk-next/install`. `~/rexglue-sdk` is not touched; the other agents keep using
it until the update is merged. Then the SDK's `unit_tests` (`-DREXGLUE_BUILD_TESTS=ON` in that
checkout, a separate build directory, no install).

Done on 2026-10-08 (Linux, all configurations): the series applies, the SDK builds and installs,
and `unit_tests` (Release) gives 245 cases, 236 passed, 5 failed, 4 skipped. The 5 failures
(`codegen/output_stamp_test.cpp` 227-228, four `core/chrono_test.cpp` cases on the NT epoch) are the
same 13 checks that fail with the old base's binary (`~/rexglue-sdk`, 253 cases with patch 19's
eight); no patch touches those files. The patches' own tests (`[vfs]`, `[keystroke]`, `[content]`,
`[input]`: 40 cases) pass. The xxHash include workaround is no longer needed (`b5e0cf8`).

**Heavy builds are coordinated with the render agent** (they skew its measurements): the SDK build,
the unit tests and every game build below start only when it says the machine is free. At the time
of writing it asked for no build during its measurement runs.

**The SDK's install registers its prefix in CMake's user package registry**
(`~/.cmake/packages/rexglue/<md5 of the prefix>`, `cmake/rexglue_install.cmake`, no option to turn
it off). Our `CMakeLists.txt` finds the SDK with `find_package(rexglue)` and the presets give no
`CMAKE_PREFIX_PATH`, so with two installs registered, a fresh configure can pick either one. After
every install into `~/rexglue-sdk-next`, delete its entry; builds against it pass
`-DCMAKE_PREFIX_PATH=~/rexglue-sdk-next/install`. (Done after the first build, 2026-10-08: the
entry was there from the end of the install until it was noticed; no build tree was configured
from scratch meanwhile.)

Windows: `tools/build-deps/windows.ps1 -SdkPrefix ...` from the same branch on the Windows machine
(patches 17 and 18 only build there). Not possible from this machine.

### 3. Build the game against it

A separate worktree of the branch (codegen writes `generated/` in the source tree, so a shared tree
would mix the two bases), with `-DCMAKE_PREFIX_PATH=~/rexglue-sdk-next/install` and the codegen
of the new SDK (`~/rexglue-sdk-next/install/bin/rexglue codegen torchlight_manifest.toml`), using
the manifest options the codegen task settled on. The project's tests (`ctest`) and the replays of
the capture set, compared with the same replays on the old base.

### 4. Overlaps to check in our code

| Upstream change | Our code | What to do |
|---|---|---|
| `ReXApp`'s active callback now also returns false without window focus (`1406e1b`) | `src/live/install.cpp` replaces that callback in both modes: `InstallDialogs` (only mode, line 103: `!g_guest_input_blocked`) and `BlockGuestInputUnderXamDialogs` (Xenos, line 160). After the update the focus check is lost in both | Add the focus check to ours (the window `ReXApp` gives us has `HasFocus()`), or compose with the previous callback. Verify: with the window unfocused, keyboard and pad do not reach the game; in only mode, the focus seen through our child window (X11) or the window itself (Wayland) |
| Guest input blocked while a XAM dialog is up, at the XAM layer, both modes (`3cd7243`) | `BlockGuestInputUnderXamDialogs` (Xenos: `xeXamIsUIActive`) and `BlockGuestInput` (only mode: ImGui dialogs, plus the pad's consumed buttons) | Ours stay at first (they also cover the ImGui overlays). Check that the button that closes a dialog does not reach the game twice and is not lost; if upstream covers XAM dialogs, the Xenos half of `BlockGuestInputUnderXamDialogs` can go in a later commit |
| MnK keystrokes upstream, no repeat for bound keys (`3f34ffc`) | Patch 19 removed | In the game with the keyboard (`--mnk_mode=true`), Linux and Windows: the first menu, the character list, a held direction in a list. If hold-to-repeat is missed: a small upstream PR on top of D13 (`keystroke_repeat.h`), not a local patch |
| Window size cvars live, `Window::Create` without a size, display mode switch in fullscreen (`289f518`, `923c1a5`, `1406e1b`) | `platform::FindGameWindow`, the child window and its size (`live/install.cpp` ~516, `live_mode.cpp` ~213-224) | Start in windowed and fullscreen, both modes; resize; check the native backend follows the window. Changing `window_width` at run time now resizes the window: check our child follows |
| `LogConfig` (`b971840`) | `install.cpp` ~408-412 sets `log_file` through `SetFlagByName` before the runtime starts; `torchlight_app.h:142` and `install.cpp:504` read it | Expected to keep working (`ApplyLogCvarOverrides` reads it when non-default). Check the log and the OGRE log land where they did, with the numbering. **The file sink no longer rotates** (`basic_file_sink`; `log_max_file_size_mb` and `log_max_files` are gone), and `dir_budget_bytes` prunes only when the runtime names the file, which ours does not: a run's log has no size limit on `bd833a2`. Restore a limit before moving the base (`docs/crash-handling.md` on `docs/crash-handling`, section 5) |
| Input focus inside the SDL and MnK drivers, vibration, deadzones | `platform/` reads the pad for the dialogs (`platform::ReadGamepad`) | Vibration in the game (it now works through the SDK); no deadzone applied twice |

### 5. The cost of the new fences (`mfence`)

Only `sync` and `eieio` add an instruction on x86-64 (`mfence`, tens of cycles); `lwsync` becomes a
compiler barrier and the `stwcx.` change keeps a `lock cmpxchg` as before. What matters is how often
the fenced paths run (the guest's locks and its GPU ring writes), not the 33 static sites.

Method of `docs/performance-profile.md` (RelWithDebInfo, `--native_skip_guest_d3d=true`, the
fixed-floor saved game; main menu, dungeon still, fight, town still, town walking; two runs each,
interleaved), with three builds that share the manifest options:

- **A**: the current base (`0c7b01a` + series).
- **B**: the new base (`bd833a2` + rebased series).
- **C**: B regenerated with `sync`/`eieio` emitting nothing, as before `bd833a2`: a one-line
  change in the scratch checkout's `build_sync`/`build_eieio`, used only for this measurement, never
  in `series`.

B against C is the cost of the fences; A against B is the whole update. Plus one Xenos-mode check
of B against C in the main menu and the dungeon (the GPU ring writes are where `eieio` sits), and
one Windows run of B.

Reading: B within the noise of C, accept. If C is clearly faster, the place to fix it is upstream,
not a local patch: on PowerPC `eieio` orders stores to ordinary memory among themselves (and
device accesses among themselves); it does not give the StoreLoad ordering that `seq_cst`
(`mfence`) adds, so a release fence (nothing on x86-64) would match it; `sync` is a full barrier and `mfence` is its right mapping. That would be one more
draft (codegen, `bd833a2`'s follow-up), with the measurement as its evidence.

### 6. Validation and merge

- SDK `unit_tests` (Linux, Windows), the project's tests, the replays: as in 2 and 3.
- Game runs, agreed beforehand: Xenos and native, Linux; native on Windows. The overlaps of 4,
  quitting from the menu (patch 2), a save and a character deletion (13, 14), a XAM dialog open at
  exit (9).
- The render agent integrates `sdk/rexglue-next`. Installing the new SDK into `~/rexglue-sdk` (or
  pointing the builds at the new prefix) happens then, agreed with the agents that share it.
- Afterwards: rebase the drafts on `development` and hand them over for review (D4 and D17 with
  special care).
- The shared SDK copies on the Linux machine (2026-10-10). The live SDK is `bd833a2` with the
  series through 30 (21 included). Two older copies stay next to it, with no build tree pointing
  at either:
  - `rexglue-sdk-bd833a2-29`, the series through 29 without 21: to roll back the last swap (two
    `mv`). It goes at the next swap, when the copy it replaces becomes the rollback.
  - `rexglue-sdk-0c7b01a`, the old base: `v0.1.0-beta` on `main` is built on it, to reproduce
    or fix something of that beta. It can go after the next release.

  The copies of 1-26, 1-27 and 1-28, an old install prefix and a scratch checkout of patch 20
  were deleted (its changes are patch 20 on `feature/pc-mods`).

### 7. For the integrator: patch numbers (2026-10-08)

Three branches added SDK patches with clashing numbers. Numbers are now handed out from one table
(`patches/README.md`, "Numbers"), kept by the patches maintainer, and are never reused:

| # | Patch | Branch | Base it applies on |
|---|---|---|---|
| 19 | `rexglue-mnk-keystrokes.patch` | `develop` | `0c7b01a` only; removed on `bd833a2` |
| 20 | `rexglue-vfs-wildcard-dos-semantics.patch` | `feature/pc-mods` | `0c7b01a` and `bd833a2` |
| 21 | `rexglue-sdl-software-renderer.patch` | `feature/launcher-imgui` | `0c7b01a` and `bd833a2` (with or without 18) |
| 22 | `rexglue-tests-portable.patch` (was 21 on `sdk/rexglue-next` until this change) | `sdk/rexglue-next` | `bd833a2` |
| 23 | `rexglue-fctiw-rounding-mode.patch` (D22) | `sdk/rexglue-next` | `bd833a2` (needs 22 for its tests on macOS) |
| 24 | `rexglue-arm64-mffs-rounding.patch` (D23) | `sdk/rexglue-next` | `bd833a2` |
| 25 | `rexglue-mtfsf-field-mask.patch` (D24) | `sdk/rexglue-next` | `bd833a2` |
| 26 | `rexglue-log-rotation.patch` | `sdk/rexglue-next` | `bd833a2` |
| 27 | `rexglue-guest-file-flush.patch` (D27) | `sdk/guest-file-flush` | `bd833a2` |
| 28 | `rexglue-quiet-missing-files.patch` (D28) | `sdk/series-review` | `bd833a2` |
| 29 | `rexglue-case-variants.patch` (D30) | `sdk/case-variants` | `bd833a2` |
| 30 | `rexglue-posix-chain-unclaimed-faults.patch` (D25) | `sdk/fault-chain` | `bd833a2` |
| 31 | `rexglue-guest-fatal-hook.patch` (D31) | `sdk/fatal-errors` | `bd833a2`, after 30 |
| 32 | next free | | |

22-27 are in `develop`; their branches were merged and deleted on 2026-10-09.

Checked with `git apply --check`: 20 and 21 apply on top of the rebased series (`bd833a2`), and so
does 22; the three touch different files (`src/filesystem`, `thirdparty/CMakeLists.txt`,
`tests/`), so they apply in any order.

Merging these branches conflicts in `patches/series` and `patches/README.md`, and the resolution
is mechanical:
- `series`: the lines in number order, ending `... 18, 20, 21, 22`. Keep 19 (`rexglue-mnk-keystrokes.patch`)
  only while `SDK_COMMIT` is `0c7b01a`. 22 needs `bd833a2` and comes in with `sdk/rexglue-next`.
- `README.md`: keep the "Numbers" table from `sdk/rexglue-next`. Where one side has the "Taken by"
  placeholder for 20 or 21 and the other has the full text, keep the full text.
- `tools/deps/key.sh` hashes `series` and the patches, so each merge changes the dependency key
  once. That is expected.

Ask the patches maintainer before integrating any of these branches. If another branch adds a
patch, it also asks there for a number.

### 8. For the macOS and Windows agents

There is no direct channel between machines. The user carries messages, and what is meant for
these agents lives here and in `patches/README.md`.

- **macOS (ARM64).** Base: `develop` (series on `bd833a2`, patches 22-27 included). `sdk/rexglue-next`
  and `sdk/guest-file-flush` were merged into `develop` and deleted on 2026-10-09.
  - The PPC test data is on branch `sdk/ppc-test-data`. Build with
    `-DREXGLUE_BUILD_TESTS=ON -DREXGLUE_PPC_TEST_BIN_DIR=<that checkout>/bin`. To remake it after an
    SDK change, run `tools/deps/build_ppc_test_data.sh` on Linux.
  - Received from `docs/macos-port.md` (`docs/macos-port-plan`, 9f6ab66). The
    `codegen_writer_test.cpp:128` error is Catch2's `operator<<` being ambiguous for the `__int128`
    duration of `file_time_type`, which patch 22 avoids. With patch 22, `ppc_tests` passes on
    ARM64 (1462 cases), and `unit_tests` fails only the `output_stamp_test.cpp` checks shared with
    x86-64. The ARM64 bugs are now drafts D22, D23 and D25. D24 (`mtfsf` mask) was found here on
    the way; it affects every architecture. The `tests/ppc` suite covers none of them, so
    `ppc_tests` passing on ARM64 does not clear them.
  - **Patches 23-25 (2026-10-08, revised 2026-10-09): results received** (`docs/macos-port.md` on
    `docs/macos-port-plan`, 6927cec): as expected, except `fctix_rounding` without the fixes, 12 of
    20 on ARM64 and not 14 (the 2^63 cases pass there, D22). With the fixes, `ppc_tests` 1492 of
    1492, `unit_tests` only `output_stamp_test.cpp:227-228`, `chrono_test` passes (D29), `[flush]` 6
    of 6. The steps stay below for a rerun after a series change. On x86-64 the rounding tests of 23
    and the tests of 24 cannot fail, because native SSE2 and MXCSR were right there. Only ARM64
    shows those two bugs.
    1. Take `develop`, and `sdk/ppc-test-data` at `0ffbb64` or later (169 files of each kind, and
       `bin/sources.sha256`). The configure stops if a `.bin` is missing or a test source does not
       match the binaries.
    2. Build the SDK as before with the whole series (`tools/deps/build_sdk.sh`, which applies
       23-25). Then, in its checkout, take the fixes out but keep their tests:

           for p in rexglue-mtfsf-field-mask rexglue-arm64-mffs-rounding rexglue-fctiw-rounding-mode; do
             git apply -R --exclude='tests/*' <repo>/patches/$p.patch
           done

       Build `ppc_tests` with `-DREXGLUE_BUILD_TESTS=ON -DREXGLUE_PPC_TEST_BIN_DIR=...` and run
       `ppc_tests "fctix_rounding.*"`, then `"mffs_rounding.*"`, then `"mtfsf_fields.*"`.
       Expected without the fixes:
       - **`fctix_rounding`: 12 of 20 fail.** These fail: 2.5 and -2.5 to nearest; 2.7 and -2.7
         toward zero; -2.5 up; 2.5 down. Each fails for both `fctiw` and `fctid`. The two cases
         of 2^63 (`fctid` and `fctidz`) pass on ARM64 and fail on x86-64 (D22).
       - **`mffs_rounding`: 4 of 6 fail.** These fail: up, down, and both save-and-restore cases.
         Nearest and toward zero pass.
       - **`mtfsf_fields`: 4 of 4 fail**, as on x86-64.
    3. Put the fixes back (the same loop without `-R`, in the order 23, 24, 25), rebuild and run all
       of `ppc_tests` (expected: 1492 cases pass) and `unit_tests` (expected: as before, only
       `output_stamp_test.cpp:227-228`). On Linux x86-64, Release, `chrono_test.cpp` also fails at
       the NT epoch (1601), on a clean `bd833a2` too (D29); it passes on macOS.
    4. Anything else is a finding: send the failing cases' output.
  - **Patch 27 (guest file flushes): done on macOS** (MAC.7, `docs/macos-port.md` on
    `docs/macos-port-plan`, 2078e54). `[flush]` 6 of 6 on APFS, and in the game two saves and
    loads, each with its flush lines (D27). The `F_FULLFSYNC` timing in a game save
    (`fs_usage`) was left out by the owner's decision.
- **Windows.** The SDL software renderer patch is now number 21 for good. The tests patch moved to
  22. Your version of 21 with Metal on Apple (`5a98b6f`) is now the last line of the `bd833a2`
  series (2026-10-09); it applies after the two Windows patches. When you next touch the series,
  ask for a number here first.

Cost: the series is done; what remains is builds (SDK in all configurations, three game builds
for the fence measurement), the measurement runs and the game validation. About a day, most of it
waiting for the machine to be free.
