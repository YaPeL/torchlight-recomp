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
| `fctiw`/`fctid` round half away from zero on ARM64 (D22) | Still there | Issue and PR |
| `mffs` swaps round up and down on ARM64 (D23) | Still there | Issue and PR |
| `mtfsf` applies its field mask reversed (D24, all architectures) | Still there | Issue and PR |
| An unclaimed host fault hangs instead of crashing on POSIX (D25) | Still there | Issue and PR |
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

**PR: `fix(codegen): fctiw/fctid round in the current rounding mode on every architecture`**

Fixes #NNN. Convert with `std::nearbyint` (or `llrint`), which honours the FP environment that
`storeFromGuest` sets on both architectures, and keep today's NaN and saturation handling. The
generated code must not let the compiler fold the conversion across an `mtfsf`, so either keep it
behind a call or compile with `-frounding-math`. Tests: `fctiw` and `fctid` of 2.5, -2.5, 3.5 and
2.7 under each of the four modes, set with `mtfsf 0xFF`.

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
| `LogConfig` (`b971840`) | `install.cpp` ~408-412 sets `log_file` through `SetFlagByName` before the runtime starts; `torchlight_app.h:142` and `install.cpp:504` read it | Expected to keep working (`ApplyLogCvarOverrides` reads it when non-default). Check the log and the OGRE log land where they did, with the numbering |
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

### 7. For the integrator: patch numbers (2026-10-08)

Three branches added SDK patches with clashing numbers. Numbers are now handed out from one table
(`patches/README.md`, "Numbers"), kept by the patches maintainer, and are never reused:

| # | Patch | Branch | Base it applies on |
|---|---|---|---|
| 19 | `rexglue-mnk-keystrokes.patch` | `develop` | `0c7b01a` only; removed on `bd833a2` |
| 20 | `rexglue-vfs-wildcard-dos-semantics.patch` | `feature/pc-mods` | `0c7b01a` and `bd833a2` |
| 21 | `rexglue-sdl-software-renderer.patch` | `feature/launcher-imgui` | `0c7b01a` and `bd833a2` (with or without 18) |
| 22 | `rexglue-tests-portable.patch` (was 21 on `sdk/rexglue-next` until this change) | `sdk/rexglue-next` | `bd833a2` |
| 23 | next free | | |

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

- **macOS (ARM64).** Base: `sdk/rexglue-next` (series on `bd833a2`, patch 22 included).
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
- **Windows.** The SDL software renderer patch is now number 21 for good. The tests patch moved to
  22. When you next touch the series, ask for a number here first.

Cost: the series is done; what remains is builds (SDK in all configurations, three game builds
for the fence measurement), the measurement runs and the game validation. About a day, most of it
waiting for the machine to be free.
