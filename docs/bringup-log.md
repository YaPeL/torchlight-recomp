# Torchlight bring-up log

Instructions: `ASTRA_BRINGUP.md` was absent; read the complete `TORCHLIGHT_RECOMP_ASTRA_HANDOFF.md`, which contains the requested immediate task.

Baseline: hint `0x82A1FE30` already present, included by the manifest, declared, defined, and registered. Initial incremental build passed. Sandbox ptrace was denied; GDB works with approved escalation. No generated code is manually edited.


## Iteration 1

- Date/time: 2026-09-06T20:50:29.789990-03:00
- Runtime result: InvalidFunctionTrap breakpoint reached
- Crash class: InvalidFunctionTrap
- Backtrace summary:
```text
#0  0x00007ffff744d090 in rex::runtime::InvalidFunctionTrap(PPCContext&, unsigned char*) () at ~/rexglue-sdk/out/install/linux-amd64/lib/librexruntime.so
#1  0x0000555558addd9e in sub_82884098 () at ~/torchlight-recomp/generated/default/torchlight_recomp.97.cpp:19379
#2  0x000055555c11a7eb in xstart () at ~/torchlight-recomp/generated/default/torchlight_recomp.203.cpp:19205
#3  0x00007ffff742f83a in rex::system::XThread::Execute() () at ~/rexglue-sdk/out/install/linux-amd64/lib/librexruntime.so
```
- `last_indirect_target`: `0x82A1FD60`
- Target in XEX range (and aligned): True
- Already registered: False
- Already hinted: False
- Hint added: `0x82A1FD60`
- Codegen result: exit 0; registered=True, defined=True, declared=True; artifact `bringup-artifacts/iteration-1-codegen.log`.
- Build result: exit 0
- Next runtime result: see iteration 2.

## Iteration 2

- Date/time: 2026-09-06T20:52:02.998722-03:00
- Runtime result: InvalidFunctionTrap breakpoint reached
- Crash class: InvalidFunctionTrap
- Backtrace summary:
```text
#0  0x00007ffff744d090 in rex::runtime::InvalidFunctionTrap(PPCContext&, unsigned char*) () at ~/rexglue-sdk/out/install/linux-amd64/lib/librexruntime.so
#1  0x0000555558adddbe in sub_82884098 () at ~/torchlight-recomp/generated/default/torchlight_recomp.97.cpp:19379
#2  0x000055555c11a87b in xstart () at ~/torchlight-recomp/generated/default/torchlight_recomp.203.cpp:19205
#3  0x00007ffff742f83a in rex::system::XThread::Execute() () at ~/rexglue-sdk/out/install/linux-amd64/lib/librexruntime.so
```
- `last_indirect_target`: `0x82C80C00`
- Target in XEX range (and aligned): True
- Already registered: False
- Already hinted: False
- Hint added: `0x82C80C00`
- Codegen result: exit 0; registered=True, defined=True, declared=True; artifact `bringup-artifacts/iteration-2-codegen.log`.
- Build result: exit 0
- Next runtime result: see iteration 3.

## Iteration 3

- Date/time: 2026-09-06T20:53:36.176531-03:00
- Runtime result: InvalidFunctionTrap breakpoint reached
- Crash class: InvalidFunctionTrap
- Backtrace summary:
```text
#0  0x00007ffff744d090 in rex::runtime::InvalidFunctionTrap(PPCContext&, unsigned char*) () at ~/rexglue-sdk/out/install/linux-amd64/lib/librexruntime.so
#1  0x0000555555da557a in sub_8277FB78 () at ~/torchlight-recomp/generated/default/torchlight_recomp.9.cpp:16737
#2  0x0000555559605d7f in sub_827802E8 () at ~/torchlight-recomp/generated/default/torchlight_recomp.119.cpp:16801
#3  0x000055555819b63d in sub_827749E0 () at ~/torchlight-recomp/generated/default/torchlight_recomp.79.cpp:16104
```
- `last_indirect_target`: `0x821DF960`
- Target in XEX range (and aligned): True
- Already registered: False
- Already hinted: False
- Hint added: `0x821DF960`
- Codegen result: exit 0; registered=True, defined=True, declared=True; artifact `bringup-artifacts/iteration-3-codegen.log`.
- Build result: exit 0
- Next runtime result: see iteration 4.

## Iteration 4

- Date/time: 2026-09-06T20:55:09.777274-03:00
- Runtime result: stopped; GDB status 124.
- Crash class: non-trap, timeout, or debugger failure; inspect artifact.
- Backtrace summary: see `bringup-artifacts/iteration-4-gdb.log`.
- `last_indirect_target`: not validly obtained
- Target in XEX range: unknown
- Already registered: unknown
- Hint added: none
- Codegen result: not run
- Build result: not run
- Next runtime result: automatic loop stopped.

## Stop-condition investigation

- Date/time: 2026-09-06, approximately 20:56–20:59 -03:00.
- Automatic hint loop stopped after three successful additions: `0x82A1FD60`, `0x82C80C00`, `0x821DF960`. The baseline `0x82A1FE30` remains. All three codegen validation stages and incremental builds passed; each emitted 5 changed files with 438 unchanged, and each target's definition/declaration/registrations were verified.
- Iteration 4 timed out at 60 seconds without hitting `InvalidFunctionTrap`. SIGINT in the artifact is the diagnostic timeout interrupt, not a game crash. The printed `$1 = 0x0` in that artifact is **not a valid guest target**, because the selected frame is the host event loop; no hint was added from it.
- A separate 25-second diagnostic run reproduced the non-trapping state and captured all thread stacks: `bringup-artifacts/hang-threads.log`. The host main thread waits in SDL/Wayland. Guest Main XThread is executing `__restgprlr_29 <- sub_82774170 <- sub_821A5C10 <- sub_82775840 <- sub_827758B8 <- sub_825803A0`. Other guest workers wait in `KeWaitForSingleObject`.
- Read-only generated-code inspection shows `sub_821A5C10` repeatedly tests pointer progress using object fields at offsets 11024 and 11036 and calls `sub_82774170` while the condition remains true (`torchlight_recomp.31.cpp`, around line 155). This supports a guest busy-wait, rather than the SDL event wait being the cause.
- Runtime log evidence is preserved in `bringup-artifacts/hang-runtime.log`: `VdSetGraphicsInterruptCallback`, `VdInitializeRingBuffer`, and `VdEnableRingBufferRPtrWriteBack` each report **no GPU emulation loaded (gpu_plugin not set); call ignored**.
- SDK source confirms `gpu_plugin` defaults to an empty string, disabling GPU emulation (`~/rexglue-sdk/src/ui/rex_app.cpp:49`). `SetupPresentation` loads a plugin only when this value is nonempty. The video exports return without initializing the ring buffer or writeback when there is no graphics system (`src/kernel/xboxkrnl/xboxkrnl_video.cpp:328`). Combined with the guest spin stack, this strongly implicates absent GPU command processing as the current blocker; no Vulkan/shader execution failure has yet been established.
- The installed plugin `~/rexglue-sdk/out/install/linux-amd64/lib/librexgpu-xenos.so` exists. The next narrow experiment would be a separately reviewed launch with `--gpu_plugin xenos`, using the existing library path and game-data root. Not performed in this task: the instructions require stopping and reporting the new subsystem blocker before changing runtime behavior.
- Secondary runtime findings: `SAVE:` is unmapped; `SAVE:\local_settings.txt` fails, `game:\appdata` returns `0xc0000022`, and `buildver.txt` is missing. These deserve separate filesystem setup investigation, but the captured main-thread stack does not show a filesystem wait.
- Meaningful visual game content was not verified. No original game data, SDK behavior, or generated source was manually patched.
- Helper: `tools/bringup.py` implements the capped loop. After the run, it was hardened to preserve iteration numbering, build before a fresh run, extract a target only at the trap frame, collect all thread stacks and runtime logs, stop on runtime subsystem errors, and refuse resumption while `bringup-artifacts/STOPPED` exists. Syntax validation passed; the final helper revision was not rerun through the hint loop because the stop condition remains active. Visual success requires human observation; the helper cannot classify window pixels.

## GPU continuation — 2026-09-07

- First launch with `--gpu_plugin xenos` failed before guest startup: the loader searches beside the executable, not the SDK library directory (`xenos-first-gdb.log`, runtime `torchlight_011.log`).
- Narrow build fix: `CMakeLists.txt` now passes `GPU_PLUGINS xenos` to `rexglue_setup_target`. Configure/build passed; SDK staging copied the installed plugin beside the executable. Codegen reported the module up to date; no generated source was manually changed.
- Retry with `--gpu_plugin xenos --log_level debug` confirms `GPU plugin 'xenos' loaded`, Vulkan instance API 1.4.341, Intel UHD Graphics 630 device API 1.4.335, and swapchains at 1280x720 then 1920x1080. GPU Commands and GPU VSync threads exist. No GPU initialization failure was observed. Evidence: `xenos-staged-gdb.log`, runtime `torchlight_012.log`.
- The guest advanced beyond yesterday's `sub_821A5C10` spin and reached a new InvalidFunctionTrap through `sub_82536918`. The host UI thread was in `VulkanPresenter::PaintAndPresentImpl`; actual displayed pixels have not yet been verified.

## Iteration 5

- Date/time: 2026-09-07, approximately 13:20 -03:00.
- Runtime result / crash class: new InvalidFunctionTrap, reproduced with Xenos enabled.
- Backtrace summary: `sub_82536918 <- sub_8252A7A0 <- sub_8252AA20 <- sub_825C9AA0`, continuing through a deeper startup chain (see `bringup-artifacts/xenos-target-gdb.log`).
- `last_indirect_target`: `0x82536798`; LR `0x82536948`.
- Target in XEX range: yes, aligned.
- Already registered or hinted: no; no existing address match in generated/default or config.
- Hint added: `0x82536798 = {}`. Actual new trap satisfies the user's condition for resuming narrow function-hint bring-up.
- Codegen/build/next runtime: pending.
- Iteration 5 completion: codegen passed (6 written, 437 unchanged), declaration/definition/both registrations verified, no overlap/conflict diagnostics, configure/build passed.
- Next runtime: new InvalidFunctionTrap target `0x825459F8` from the same call site, LR `0x82536948`. Evidence: `bringup-artifacts/iteration-6-observed-gdb.log`. The earlier `xenos-target-gdb.log` was reused by the repeat command; the initial target and LR were captured above before reuse, and the original all-thread backtrace remains in `xenos-staged-gdb.log`.

## Iteration 6

- Date/time: 2026-09-07T13:23:11.136187-03:00
- Runtime result: InvalidFunctionTrap breakpoint reached
- Crash class: InvalidFunctionTrap
- Backtrace summary:
```text
#0  0x00007ffff744d090 in rex::runtime::InvalidFunctionTrap(PPCContext&, unsigned char*) () at ~/rexglue-sdk/out/install/linux-amd64/lib/librexruntime.so
#1  0x000055555830ae01 in sub_82536918 () at ~/torchlight-recomp/generated/default/torchlight_recomp.82.cpp:9924
#2  0x000055555b5bc2b7 in sub_8252A7A0 () at ~/torchlight-recomp/generated/default/torchlight_recomp.181.cpp:10064
#3  0x000055555c0f9c4b in sub_8252AA20 () at ~/torchlight-recomp/generated/default/torchlight_recomp.203.cpp:9824
```
- `last_indirect_target`: `0x825459F8`
- Target in XEX range (and aligned): True
- Already registered: False
- Already hinted: False
- Hint added: `0x825459F8`
- Codegen result: exit 0; registered=True, defined=True, declared=True; artifact `bringup-artifacts/iteration-6-codegen.log`.
- Build result: exit 0
- Next runtime result: see iteration 7.

## Iteration 7

- Date/time: 2026-09-07T13:24:51.064243-03:00
- Runtime result: stopped; GDB status 0.
- Crash class: non-trap, timeout, or debugger failure; inspect artifact.
- Backtrace summary: see `bringup-artifacts/iteration-7-gdb.log`.
- `last_indirect_target`: not validly obtained
- Target in XEX range: unknown
- Already registered: unknown
- Hint added: none
- Codegen result: not run
- Build result: not run
- Next runtime result: automatic loop stopped.

### Iteration 7 investigation: handled protection faults

The loop stopped at GDB's first SIGSEGV, before ReXGlue's installed handler could run. This is not sufficient evidence of a fatal crash. Read-only inspection of `src/core/exception_handler_posix.cpp` and `src/system/mmio_handler.cpp` confirms access-violation handling for guest memory/MMIO/write watches. A diagnostic rerun captured a protection fault (`si_code=2`, host address `0x1BEF702A4`) at `sub_821FA128` and delivered SIGSEGV to the existing handler. Execution advanced to a new InvalidFunctionTrap target `0x828FFD70` via `sub_82862038 <- sub_82915C08 <- sub_828B5BB0`. Evidence: `bringup-artifacts/xenos-segv-diagnosis.log`.

This corrects the preliminary crash classification: the sampled SIGSEGVs are consistent with handled GPU memory protection, not an established fatal blocker. GDB now passes SIGSEGV to the existing handler for Xenos runs; no signal handler or runtime behavior was patched. Fatal process termination, aborts, new traps and the timeout remain observable. The loop may resume because a new actual InvalidFunctionTrap was observed. Nonblocking filesystem lookup diagnostics are retained in artifacts; SAVE setup remains untouched as requested.

## Iteration 8

- Date/time: 2026-09-07T13:27:06.026375-03:00
- Runtime result: InvalidFunctionTrap breakpoint reached
- Crash class: InvalidFunctionTrap
- Backtrace summary:
```text
#0  0x00007ffff744d090 in rex::runtime::InvalidFunctionTrap(PPCContext&, unsigned char*) () at ~/rexglue-sdk/out/install/linux-amd64/lib/librexruntime.so
#1  0x000055555a2f26c2 in sub_82862038 () at ~/torchlight-recomp/generated/default/torchlight_recomp.144.cpp:18737
#2  0x0000555556a9c349 in sub_82915C08 () at ~/torchlight-recomp/generated/default/torchlight_recomp.34.cpp:27947
#3  0x0000555558f72651 in sub_828B5BB0 () at ~/torchlight-recomp/generated/default/torchlight_recomp.106.cpp:19910
```
- `last_indirect_target`: `0x828FFD70`
- Target in XEX range (and aligned): True
- Already registered: False
- Already hinted: False
- Hint added: `0x828FFD70`
- Codegen result: exit 0; registered=True, defined=True, declared=True; artifact `bringup-artifacts/iteration-8-codegen.log`.
- Build result: exit 0
- Next runtime result: see iteration 9.

## Iteration 9

- Date/time: 2026-09-07T13:28:55.850699-03:00
- Runtime result: stopped; GDB status 124.
- Crash class: non-trap, timeout, or debugger failure; inspect artifact.
- Backtrace summary: see `bringup-artifacts/iteration-9-gdb.log`.
- `last_indirect_target`: not validly obtained
- Target in XEX range: unknown
- Already registered: unknown
- Hint added: none
- Codegen result: not run
- Build result: not run
- Next runtime result: automatic loop stopped.

## Visible framebuffer milestone — achieved 2026-09-07

- Today's successful function hints: `0x82536798`, `0x825459F8`, `0x828FFD70`. Each was obtained from an actual InvalidFunctionTrap, validated as an aligned missing entry in range, regenerated, registered, and rebuilt successfully. No guessed boundaries or manual generated-code edits.
- Iteration 9 ended at the 60-second observation limit, but **this was not evidence of a hung guest**. Its runtime artifact contains 1,498 `XELOG_GPU PRESENT` records, beginning 13:29:05.506 and continuing through 13:29:55.840. Completed records report 1280x720 source/output images. No GPU errors were found in that run.
- Comparison with yesterday: Main XThread was sampled in `__imp__sub_82884320 <- sub_821A7138 <- sub_821A71E8`, a vectorized memory-copy path, not `sub_821A5C10`'s pointer-progress spin. GPU Commands was inside `ExecutePacketType3_WAIT_REG_MEM`, while ongoing frame presentations demonstrate forward progress across the observation period. This sample alone does not establish a stuck GPU wait or deadlock. SIGINT was the diagnostic timeout, not a spontaneous game failure.
- GNOME's screenshot D-Bus API denied focused-window capture (`ScreenshotWindow is not allowed`). A separate, bounded verification launch used `SDL_VIDEODRIVER=x11`; it retained `--gpu_plugin xenos --log_level debug`, the same game-data root, SDK, executable and GPU plugin. No runtime patch was needed. The first capture attempt missed the bounded window lifetime; the coordinated second launch captured it successfully at approximately 13:47 -03:00.
- **Verified screenshot** (1920x1080; not kept in the repository: game content). Visually inspected: Torchlight logo, “Loading”, full illustrated background, and readable equipment-requirements tip. This is meaningful game framebuffer output, not a blank swapchain or host-only UI.
- Screenshot-run runtime evidence: the run's runtime log and launch console (local artifacts, not kept in the repository). The verification process was terminated after capture. The Wayland run independently demonstrated continuous guest presentation; the screenshot directly verifies X11 window pixels.
- Stop condition: meaningful visual content reached. No unresolved Xenos/Vulkan initialization failure or fatal runtime blocker has been established after the three hints. Progression beyond the loading screen is not verified and is the next milestone, not a diagnosed hang. SAVE remains untouched and is not established as the blocker.
- Durable build change: `rexglue_setup_target(torchlight GPU_PLUGINS xenos)` stages the installed plugin beside the executable. Launch with `LD_LIBRARY_PATH=$HOME/rexglue-sdk/out/install/linux-amd64/lib ./out/build/linux-amd64-debug/torchlight --game_data_root $HOME/360tools/extracted/extracted --gpu_plugin xenos`. Prefix with `SDL_VIDEODRIVER=x11` to reproduce the captured display backend.
- `tools/bringup.py` now supports explicit `--gpu-plugin xenos --resume-after-review`, preserves the plugin across retries, lets the existing signal handler receive SIGSEGV in Xenos mode, and records concise trap stacks plus complete runtime/debugger artifacts. Syntax validation passed and the revised flow was exercised through iterations 8–9. The STOPPED marker remains active; do not resume the loop without a newly observed missing-function trap and review of the current state.
- No invasive SDK/runtime changes, game-data edits, or SAVE configuration changes were made.

## Gameplay-entry crash investigation — 2026-09-10

- User reports working Xbox controller, menu navigation, and character creation; starting gameplay afterward aborts with “Aborted (core dumped)”.
- Launched the existing known-good executable under interactive GDB with no timeout, `--game_data_root $HOME/360tools/extracted/extracted --gpu_plugin xenos --log_level debug`, and the installed SDK library path.
- GDB passes SIGSEGV to ReXGlue's existing GPU protection handler (previously verified necessary), stops on SIGABRT and InvalidFunctionTrap, and retains the process for investigation. Capture commands: `bringup-artifacts/gameplay-entry/capture.gdb`; debugger output: `bringup-artifacts/gameplay-entry/gdb.log`.
- Awaiting manual gameplay-entry reproduction. No hints, generated code, or runtime changes made.

### First concrete root-cause candidate: missing callback `0x82227C00`

- Reproduced on Main XThread, stopped at `rex::runtime::InvalidFunctionTrap` before its fatal handler executes. Importantly, the previous manual-run log (`torchlight_022.log`, 2026-09-09 19:11:13.479) independently records `[FATAL] Call to invalid or unregistered function at guest address 0x82227C00`. This matches the newly captured target exactly and links the debugger stop to the reported abort.
- Native chain: `InvalidFunctionTrap <- sub_8225B168 <- sub_8225AE60 <- sub_8224F068 <- sub_822574F0 <- sub_82257C10 <- sub_822558B0`, continuing through gameplay loading to `xstart`. Full native and all-thread backtraces, host registers, and the complete PPCContext are saved in `bringup-artifacts/gameplay-entry/gdb.log`.
- Guest context: `last_indirect_target = CTR = r11 = 0x82227C00`, `LR = 0x8225B228`, `r30 = 0x42C85E48`, `r31 = r3 = 0x471CE600`, `r4 = 0x7018C560`, `r5 = 4`, guest SP `r1 = 0x7018C510`.
- Read-only call-site inspection (`generated/default/torchlight_recomp.131.cpp:2035`) shows `lwz r11,68(r30); mr r3,r31; mtctr r11; bctrl`. The live callback slot at guest `0x42C85E8C` contains big-endian bytes `82 22 7C 00`, confirming the pointer's source.
- Target is aligned and within the XEX range. A case-insensitive search found no `82227C00` entry in config or generated output. Guest memory at target contains PPC words `2B030000 4D9A0020 81640000 91630140 4E800020`, consistent with a short null-checked setter (load from r4, store at r3+0x140, return). Evidence favors a real missing function entry rather than an out-of-range/corrupt indirect target; semantic naming is not established.
- Runtime source `src/system/function_dispatcher.cpp:37` confirms InvalidFunctionTrap invokes REX_FATAL for the missing target. The prior manual log establishes that this path produced the actual fatal event. No need to resume the stopped process merely to reproduce the abort again.
- Current runtime/Xenos/filesystem log preserved as `bringup-artifacts/gameplay-entry/runtime.log`; prior fatal log as `bringup-artifacts/gameplay-entry/prior-manual-runtime.log`. Xenos loads and Vulkan presentation initializes. Filesystem lookup warnings remain recorded but do not explain the confirmed missing callback; no SAVE changes made.
- Stopped before patching as requested. No function hints, generated edits, rebuilds, or runtime changes made in this investigation. Proposed next narrow fix, after review: seed only `0x82227C00` through the function configuration, regenerate/rebuild, and manually retest gameplay entry.
- GDB and the inferior remain paused at the original trap (session 94616, inferior PID 33301) for further inspection; logging was flushed/disabled after capture. Do not mistake the frozen window for a fresh hang.

## Iteration 10 — gameplay callback fix

- Date: 2026-09-10. User authorizes repeated validated callback hints until controllable gameplay or a different fatal blocker; menu/loading output is no longer a completion condition.
- Confirmed trap from preceding diagnosis: `0x82227C00`, LR `0x8225B228`, called by `sub_8225B168`. Rechecked range/alignment and absence from config, declarations, and both registration files.
- Hint added: `0x82227C00 = {}` through `config/torchlight_functions.toml`.
- Closed the previous paused debugger/inferior before rebuilding. Codegen/build/retest pending.
- Codegen passed (5 written, 438 unchanged), declaration/definition and both registrations verified; configure passed.
- User clarified that all menu/gameplay input will come from the physical controller. No keyboard emulation will be enabled or used. Removed the unused X11 helper draft; it was never executed against the game. Input/runtime code remains unchanged.
- Initial incremental build failed because `/usr/include/stdint.h` changed since the cached precompiled header was built. Triggered PCH regeneration by touching the build-tree `cmake_pch.hxx`; rebuilding affected objects. This is build-cache maintenance, not a generated guest-code edit.
- PCH refresh/full affected-object rebuild passed. Relaunched under GDB with Xenos, physical-controller input only, no timeout. Capture file: `bringup-artifacts/iteration-10-gdb.log`. Awaiting manual gameplay-entry interaction.
- Iteration 10 retest result: progressed beyond `0x82227C00`; new actual InvalidFunctionTrap `0x8223FF10`, again at `sub_8225B168`, with nested callback/loading frames. Captured full context/backtraces in iteration-10-gdb.log and preserved runtime log rotation files `gameplay-entry/torchlight_024*.log`.

## Iteration 11

- Date: 2026-09-10. Actual trapped target `0x8223FF10` is aligned/in XEX range and absent from config and both generated registration tables.
- Hint added: `0x8223FF10 = {}`. No guessed boundaries or generated-source edits. Codegen/build/retest pending.
- Iteration 11 codegen passed (5 written, 438 unchanged); declaration, definition, and both registrations verified. Configure/incremental build passed.
- Relaunched under GDB, no timeout, unchanged physical-controller input. Waiting for manual gameplay entry; capture: iteration-11-gdb.log.

## NVIDIA minimap observation — 2026-09-11

- User confirms controllable gameplay after iteration 11. Current blocker is a full-screen red tint with the minimap enabled on NVIDIA; Intel reportedly does not reproduce. No new crash or InvalidFunctionTrap; no hints added.
- All diagnostic launches explicitly select `/usr/share/vulkan/icd.d/nvidia_icd.json`, installed SDK `LD_LIBRARY_PATH`, extracted game root and `--gpu_plugin xenos`. Runtime logs identify NVIDIA GeForce GTX 1050 Ti with Max-Q Design, driver 580.178.04. Physical controller only.
- Temporary opt-in `TORCHLIGHT_DIAGNOSTICS=1` application forwarding hooks in `src/diagnostics.cpp`: input wrapper `sub_821FEA58` records successful R3 button edges (0x80); automap visibility setter `sub_822E6090` records object+92 before/after only when changed; cycle wrapper `sub_822E9E68` records final visibility and mode byte+137. Original functions receive the original context, and hooks do not write guest state. Mode changes can legitimately call OFF then ON within one cycle; these are distinct from final visible state.
- Confirmed second-run initial visibility OFF→ON at monotonic 1517.313198. R3 press 1556.893581 leads ON mode0→ON mode1; press 1561.274431 leads OFF; press 1566.655236 leads ON mode0; press 1570.790075 leads ON mode1. Release edges captured separately. This agrees with user's prior red→red→clear/no-map→red/map observation; pixel appearance is user-reported, not inferred from the visibility byte.
- Second diagnostic inferior 14945 exited normally; GDB's subsequent “No stack” is the post-exit `bt`, not a crash. Artifact directory `bringup-artifacts/nvidia-minimap/` contains events.log, gdb-confirmed.log, runtime rotations, binary draw snapshots and decoded JSON.
- Existing FPS instrumentation counts guest VdSwap submissions, consumed XE_SWAP packets, and successful/suboptimal actual vkQueuePresentKHR calls. First-run gameplay measured about 24–26 guest submissions/s versus 600+ host presents/s; this is guest rendering progression, not verified simulation tick rate. Counters/timing/pacing remain unchanged, and FPS work is paused.
- SDK observation callbacks are three weak, default-visible call sites in graphics/command_processor.cpp, graphics/vulkan/command_processor.cpp and ui/vulkan/vulkan_presenter.cpp. Original source backups under nvidia-minimap/sdk-originals, original installed libraries under out/diagnostic-sdk-originals. SDK runtime/Xenos Release and application Debug builds passed. No manually modified generated guest code.
- Snapshot comparison: first run ON mode1 adds two composition draws and copy/resolve operations. Blend 0x05040504 uses source-color/inverse-source-color, followed by 0x07060706 alpha blending. Second run ON mode0 includes stencil/mask draws. Guest targets change between RB_SURFACE_INFO 0x0A020280 and 0x14000500, same color EDRAM base. In second-run aligned final 26 HUD records, selected blend/target/viewport/color-mask registers and all pixel constant dwords match OFF. No guest-state leak established; Vulkan target transfers and minimap composition remain under investigation. Draw snapshots include kCopy operations whose shader hashes are stale, not actual draw shaders. Capturing a frame has I/O overhead; exclude nearby FPS samples from performance conclusions.

### Continued tint diagnosis / validation

- Validation-only NVIDIA/Wayland run (`torchlight_037.log`, copied to `nvidia-minimap/validation-runtime.log`) reproduces the tint: user confirms red with minimap ON, normal OFF. R3 epochs 1–4 and confirmed visibility/mode cycles logged at monotonic 608.885–611.951. No new InvalidFunctionTrap.
- Validation reports VUID-vkQueueSubmit-pSignalSemaphores-00067 during presentation before gameplay; no additional VUID category appears across minimap toggles. This is not yet causal evidence for the minimap-specific tint and remains untouched.
- Portable RenderDoc 1.46 and validation layer 1.4.341.0 downloaded/extracted into /tmp; no system package installation. RenderDoc initially fails instance creation with ErrorExtensionNotPresent (both SDL Wayland and X11). SDK requests Wayland extensions even for X11; binary RenderDoc does not support Wayland WSI. Preparing opt-in TORCHLIGHT_CAPTURE_X11 instance gate, omitting Wayland/portability only for capture runs. This is a temporary diagnostic exception, not the tint fix. Normal/final tests retain original window path and exact NVIDIA environment.

### Concrete root cause and narrow fix — 2026-09-12

- Successful NVIDIA X11 RenderDoc captures: `epoch-1_capture.rdc` (ON mode1) and `epoch-2_capture.rdc` (OFF). User confirms same red tint on X11 capture path. Initial time-based capture contained only host presentation and is not used to infer guest drawing; subsequent captures span three guest swap boundaries.
- ON capture event 1221 has pixel (400,200) RGBA=(38,34,24,255); after event 1571 it is (255,34,24,255), before minimap draws 1591/1603. The first inspected post-round-trip image (`event-1571-target-0.png`) visibly has red=255 over the affected rectangle. Remaining channels are preserved. Event 1571 is an internal depth/stencil→color ownership transfer, not a guest blend draw.
- Root cause: `VulkanRenderTargetCache::GetTransferShader` loads only `source_stencil` for depth/stencil→stencil-bit transfers, but the 32bpp packing branch handles only color or a loaded depth value. Therefore `packed` remains `spv::NoResult` and the final stencil-bit mask/discard block is omitted. Captured shaders at events 1483 and 1543 end immediately after fetching stencil: no stencil-mask load, conditional branch or OpKill. Their Vulkan pipelines use reference 255, REPLACE and one-bit write masks. Eight unconditional passes turn destination stencil into 0xFF. When EDRAM is later read as RGBA, stencil occupies the red byte, explaining the tint and its correlation with minimap target aliasing.
- The observed NVIDIA run uses the fallback path without VK_EXT_shader_stencil_export. Intel non-reproduction remains user-reported; we have not made an Intel reproduction run. The root cause is capability-path-specific, not an NVIDIA color-space correction.
- Six-line SDK fix routes `source_stencil[0]` into `packed` for kStencilBit before the depth branch, enabling the existing per-bit discard logic. No blend/target/pacing/input behavior or guest code changed. Maintainable patch saved in `patches/rexglue-vulkan-stencil-transfer.patch`. SDK build and retest pending.
- Separate capture-run shutdown SIGABRT was `std::system_error: Owner died` in MnkInputDriver destruction during Runtime::Shutdown, saved in gdb-capture-first.log. It is not the old gameplay-entry InvalidFunctionTrap; no hint loop or input-code work resumed.

### Verified fix result

- SDK Xenos rebuild passed; application build/codegen check passed with generated guest module unchanged. Explicitly staged the plugin beside the executable because CMake's POST_BUILD copy does not run for an SDK-only plugin rebuild. Installed and app-local plugin SHA256 both `f7cca3412c33e56197c0eef5ca9b194e88cad616cd8671f06869d0c80a12ef7c`.
- Retest used exact requested NVIDIA ICD, installed LD_LIBRARY_PATH, game root and `--gpu_plugin xenos`, original Wayland window path, observational logs and validation layer, with no RenderDoc preload and no capture-instance gate enabled. Runtime `torchlight_040.log` / saved `fixed-runtime.log` identifies NVIDIA GTX 1050 Ti with Max-Q Design. GDB artifact: `gdb-fixed.log`.
- Initial confirmed minimap visibility ON at monotonic 2030.203115. Physical R3 presses at 2066.800560, 2067.505874, 2068.057187, 2068.524577 and 2068.924292 cycle ON mode1, OFF, ON mode0, ON mode1, OFF. Original visibility setter/boolean and controller input behavior remain untouched.
- **User visual/manual verification: “All checks pass”** in response to both ON modes having no red tint, OFF looking unchanged, and gameplay rendering normally. This completes the red-tint milestone. No gameplay-entry InvalidFunctionTrap reproduced; no hints added.
- Removed the temporary capture-only Vulkan instance gate from SDK source and rebuilt the runtime; final SDK changes consist solely of the six-line stencil-transfer fix and the three existing observation callbacks. FPS counters/pacing remain unchanged. Captures and their helper scripts remain available as diagnostic artifacts, disabled in normal launches.
- Validation still reports the separate presentation semaphore reuse warning observed before the fix, plus an in-use semaphore at shutdown (VUID-vkDestroySemaphore-semaphore-05149). Guest log reports Execution complete before the latter. Neither was changed or presented as the tint cause. Prior shutdown stacks are retained; no unrelated input/runtime fixes made.

- Final SDK build/staging and whitespace/patch checks passed. App-local and installed Xenos plugin hashes still match the gameplay-tested binary after removing the capture gate.
- After the user completed the successful visual test and guest logged Execution complete, the process remained in shutdown. A bounded debugger sample shows Main thread waiting in WindowedAppContext::CallInUIThreadDeferred ← CallInUIThreadSynchronous ← MnkInputDriver destructor ← Runtime::Shutdown, consistent with the separate prior teardown issue. Stack retained in gdb-fixed.log; stopped the leftover inferior/debugger after capture. This is not a normal process-exit claim and is not a gameplay/tint regression. No input or teardown patch applied.

## Shutdown hang investigation — 2026-09-12

- Scope: Return to Game Library teardown only. Reproduction uses current known-good NVIDIA/Xenos build and extracted game root, with no TORCHLIGHT_DIAGNOSTICS, RenderDoc, capture gate or Vulkan validation layer enabled. Stencil fix/plugin SHA256 remains f7cca3412c33e56197c0eef5ca9b194e88cad616cd8671f06869d0c80a12ef7c.
- Baseline reproduced after manual gameplay/exit. GDB lifecycle breakpoints show ReXApp::OnDestroy → WindowSDL destructor → MnkInputDriver destructor → indefinite wait in CallInUIThreadDeferred. Artifacts: shutdown/baseline-gdb.log and baseline-runtime.log.
- Concrete use-after-free evidence: live app UI context is 0x7fffffffd6c8. Window 0x55555d65a1a0 is destroyed before driver 0x55555d07e040. Driver still contains the window pointer at +0x48. Freed window's former context reference at +8 now reads 0x55555d4d5660, not the real stack context. Main thread tries locking fake-context+0x18 = 0x55555d4d5678. This is a dangling-window lifecycle bug, not a legitimate UI callback waiting on another live UI thread. Allocator reuse explains earlier Owner died and mutex assertions.
- Guest completion asks the context to quit directly. ReXApp::OnDestroy removes itself as a window listener then deletes window before runtime. WindowSDL destructor calls EnterDestructor/DestroySDLWindow, which deliberately do not send OnClosing; input drivers therefore miss detachment. Mnk destructor falls back to detachment through the freed window. Context itself remains alive until after app destruction, and its synchronous method correctly runs inline on its UI thread when given a valid context.
- Narrow proposed fix: after removing ReXApp's listeners, call window RequestClose before resetting it. SDL performs close synchronously, sending existing OnClosing notifications to the still-live SDL/MnK input drivers, which detach and drain pending input callbacks. ReXApp is already removed, so its existing hard-exit OnClosing path is not invoked. No timeout, forced-exit workaround, runtime redesign, running input changes, renderer changes or FPS edits. Baseline hung inferior stopped only after recording evidence.
- Implemented the four-line change (three comment lines and RequestClose) in SDK src/ui/rex_app.cpp and its installed consumer source. Durable patch: patches/rexglue-shutdown-window-lifecycle.patch. Application Debug rebuild passed; codegen reports the guest module up to date. Both installed/app-local Xenos hashes still match the verified stencil-fix binary. First fixed manual gameplay/exit validation running under GDB; lifecycle breakpoints observe existing methods without adding runtime diagnostics.
- Fixed run 1: inferior 28215 exits normally, code 0, following Execution complete. GDB records SDLInputDriver::OnClosing before WindowSDL destruction; Mnk destruction completes. ReXApp's hard-exit OnClosing breakpoint never fires. Runtime log saved as shutdown/fixed-1-runtime.log (original torchlight_042.log), debugger sessions append to shutdown/fixed-gdb.log. Repeating with a direct DetachFromWindow breakpoint because the first run's Mnk OnClosing breakpoint did not fire (virtual-call thunk is a separate optimized entry point).
- Fixed run 2: inferior 28894 also exits normally, confirmed $_exitcode = 0. Same SDL closing-before-window-destruction sequence; no hard-exit callback, fatal signal or InvalidFunctionTrap. Runtime preserved as shutdown/fixed-2-runtime.log (torchlight_043.log). The GDB script produced a post-exit command parsing error because it was edited while sourced; this occurred after normal inferior exit and is not an application failure. Debugger was then closed.
- User confirms entering gameplay and moving with the physical controller before Return to Game Library, with gameplay and controller input normal. Two successful manual gameplay/exit repetitions complete validation. A third optional run to break on the optimized MnK listener thunk was interrupted before obtaining another result; user requested explanation and no further test is needed. Direct MnK callback breakpoint confirmation is not claimed; successful normal destruction is verified in both runs. Renderer/stencil plugin, running input behavior, FPS instrumentation and generated guest code remain unchanged.
- User subsequently requested the third test. Fixed run 3 (inferior 30124) exits normally with code 0 after Execution complete. GDB directly confirms SDLInputDriver::OnClosing, then the optimized MnK OnClosing listener thunk, both before WindowSDL destruction. MnK callback stack: ReXApp::OnDestroy → Window::RequestClose → WindowSDL::PerformClose → Window::OnBeforeClose → MnkInputDriver::OnClosing. Driver destruction then completes normally; no hard-exit callback, fatal signal or InvalidFunctionTrap. This supplies the direct MnK callback confirmation missing from the first two debugger samples. Runtime saved as shutdown/fixed-3-runtime.log (torchlight_044.log); stack and exit status appended to shutdown/fixed-gdb.log. Three normal exits verified; no additional code changes required.

## Permanent F12 debug overlay — 2026-09-12

- Added an isolated application-owned ImGui dialog and F12 listener, initialized in OnPostSetup and removed in OnShutdown before SDK UI destruction. Panel starts hidden, rejects key repeats, uses NoInputs/NoNav/NoFocusOnAppearing, and is destroyed when hidden. No controller or guest input forwarding changes.
- Guest FPS reuses the validated VdSwap forwarding/counting point. Host Present FPS uses only the existing successful/suboptimal present-result callback. No minimap, controller, draw-snapshot or capture hooks are linked; old src/diagnostics.cpp stays excluded. No timing/pacing changes or sampler thread. FPS arithmetic checks cover distinct guest/host rates, irregular intervals, zero progress, reopen and counter reset.
- GPU name comes from the active VulkanProvider device properties. Resolution reads VdSwap's guest front-buffer width/height using the SDK argument translator; it is explicitly labeled guest framebuffer. This is not host window size or a claim about all intermediate render passes. See docs/debug-overlay.md for sources, scaling limitations, SDK callback dependency and build-time disable instructions.
- Initial launch exposed application initialization ordering: OnCreateDialogs runs before Runtime exists. Captured null Runtime backtrace (debug-overlay/startup-order-gdb.log), then moved initialization to OnPostSetup. No SDK/runtime code changed. Rebuild passed; generated module remains up to date. SDK public Vulkan header also requires renderdoc_app.h, omitted by this installed package; configured its existing thirdparty include directory without enabling capture.
- Corrected NVIDIA validation run (torchlight_047.log) reaches presentation. One-shot GDB sample verifies VulkanCommandProcessor::IssueSwap → Presenter::RefreshGuestOutput dimensions 1280x720, aspect 1280:720. Active Vulkan properties and overlay both identify NVIDIA GeForce GTX 1050 Ti with Max-Q Design. Xenos SHA256 remains f7cca3412c33e56197c0eef5ca9b194e88cad616cd8671f06869d0c80a12ef7c; stencil fix and renderer binary unchanged. Manual F12/readability/controller/hidden-interval validation pending.
- Validation run exits normally, code 0. User clarifies all checks pass (“everything is perfect!”), including readability, F12/held press and controller/gameplay, and requests moving the panel from left to right. Anchored the panel to the top-right with a 12-logical-pixel margin and right-edge pivot so resizing preserves placement. This final placement-only adjustment is awaiting build verification, not another required gameplay test.
- Hidden interval from monotonic 4561.067803 to 4582.708016: 616 guest submissions / 21.640213s = 28.466 guest FPS, separately 9384 presents = 433.637 host FPS. No slowdown is apparent relative to prior ~24–26 guest FPS observations, but this is not a controlled identical-scene comparison against the feature-free binary; no exact zero-overhead performance claim. The earlier visible interval includes navigation/loading and is not a valid paired performance sample.
- Build-time disable validated: -DTORCHLIGHT_DEBUG_OVERLAY=OFF builds successfully and removes all overlay/observer definitions; __imp__VdSwap is again an unresolved SDK import. Restored enabled configuration. FPS unit check passes; runtime and GDB evidence preserved under debug-overlay/validation-*. No SDK code/binary changes for this task. patches/rexglue-host-present-observer.patch preserves the already-existing present callback dependency for fresh SDK installs.
- Final top-right placement rebuild and whitespace checks pass. Enabled overlay is staged in the current Debug executable. Generated module is still up to date, and Xenos plugin hash is unchanged. No additional manual run requested for this placement-only edit.

## Focused performance investigation — 2026-09-12

- Current known-good NVIDIA Debug build launched under GDB (inferior 38474, runtime torchlight_049.log), exact NVIDIA ICD and installed SDK library path. No timing/pacing, renderer, overlay or guest-source changes. Awaiting the user's confirmation of a repeatable actual-gameplay location before measuring; menu/loading time will not be used as the gameplay baseline.
- Compile-command inspection finds translated guest objects built with -g and no optimization flag (effective -O0). Existing RelWithDebInfo preset configured in a separate build directory for a possible -O2 comparison; no compilation started during baseline preparation. This is a candidate, not yet a measured bottleneck.
- perf is installed but kernel.perf_event_paranoid=4 blocks even user/task-clock events outside the sandbox; non-interactive sudo requires authentication. No system security settings changed. NVIDIA utilization is available outside the sandbox. Prepared tools/profile_guest.py for bounded GDB stack samples and separate uninterrupted counter/CPU intervals; profiling overhead will be excluded from FPS benchmarks.
- First run exited before a timed gameplay capture. Relaunched Debug after user confirmation of gameplay. debug-baseline.json captures 30.0177s: 17.2898 guest FPS, 480.4504 host presents/s; CPU usage in units of one core: main guest 93.21%, audio worker 73.82%, timer thread 26.05%, Xenos commands 18.36%, UI/presenter 16.79%. GPU monitor approval arrived after process exit, so debug-utilization.json has no valid gameplay GPU sample and must not be used as baseline GPU utilization.
- Built the existing RelWithDebInfo preset (-O2 -g -DNDEBUG) in its own directory without editing generated code. Installed and app-local Xenos binaries remain the same verified f7cca341... plugin. User confirms standing at the same gameplay spot. optimized-baseline.json captures 30.0162s: 43.7763 guest FPS, 217.5816 host presents/s, a 2.532x increase over Debug. Main guest CPU 85.62%, audio 73.93%, Xenos commands 35.15%, timer 24.02%, UI/presenter 8.40%. These are GDB-attached runs without periodic pauses during the 30s FPS windows; normal write-watch signals still pass through GDB, so they are not untraced benchmarks.
- Optimized NVIDIA utilization captured concurrently (optimized-utilization.json); 30 samples overlap the uninterrupted window, typically 89–95% GPU at P0, 1708MHz core / 3504MHz memory. A subsequent 60-stack sample run is separate from the FPS window (3.181s spent unwinding), and its frame rate is not used for the speedup claim. Main-thread self frames include guest copy routines sub_82860A50 (15/60), sub_82884644 (12/60), sub_82884320 (8/60). These are debugger stack occupancy counts, not perf CPU-cycle percentages. Sampling is biased by GDB/write-watch signal handling; main thread was already in traced-stop state at 48/60 pre-interrupt proc snapshots.
- Audio WaitMultiple appears in 59/60 audio stacks. Source confirms kAlertablePollSlice=1ms, while PosixConditionBase::WaitMultiple truncates the remaining interval to integer milliseconds before sleep_for. The sub-millisecond remainder thus becomes sleep_for(0), causing polling for much of each 1ms wait. Concrete secondary CPU-cost candidate; no synchronization or timer behavior patched during profiling.
- User reports a hang after the optimized sampling run, then confirms gameplay/audio/F12 checks passed. GDB actually caught SIGABRT on GPU Commands thread 44128: FatalError → GraphicsSystem::OnHostGpuLossFromAnyThread → VulkanCommandProcessor::EndSubmission → IssueSwap. Source only enters this branch for VK_ERROR_DEVICE_LOST from vkQueueSubmit. Runtime logs both Xenos and presenter submission failures at 17:19:11.798. Full fatal/all-thread stacks saved in optimized-gdb.log; runtime copied to optimized-runtime.log. Main guest was in the sub_821A5C10 progress-wait path after GPU loss. No new InvalidFunctionTrap. Kernel journal query returned no matching NVIDIA fault lines. The optimized build is therefore not yet validated safe; neither profiling-pause involvement nor a pre-existing Vulkan synchronization issue is established.
- Isolation rerun, optimized with no periodic sampling (runtime torchlight_002.log): user reports black from startup. No device-loss abort captured; log shows swapchain recreation, followed by window-close/Title terminated hard exit at 17:22:42. The process had already exited when inspected. This is not proof of the same failure category recurring. Captured logs retained as optimized-clean-*. The unchanged Debug NVIDIA build is being checked for startup rendering after the device-loss event. No runtime/audio/renderer patch applied. Full profiling findings and limits are in docs/performance-profile.md; optimized/Intel correctness validation remains incomplete.
- Recovery check: unchanged Debug build, exact NVIDIA environment (inferior 45878, torchlight_051.log). User confirms startup and menus render normally, with the same 230 cached pipelines loaded. Thus the black startup is not persistent across both executable configurations; optimization-specific conditions remain under investigation, not established root cause. Profiling pass ends without promoting the unstable optimized candidate or making renderer/synchronization changes. Intel validation of the optimized candidate was not pursued after its NVIDIA failures; no Intel-regression claim. The known-good Debug application remains available.

## Controlled performance follow-up — 2026-09-12

- User selected the saved pond location. Each run starts from its own copy of the same saved user-data/shader-cache snapshot; original saves remain untouched. Two manual laps are warm-up, followed by a stationary camera/minimap-matched measurement. Read-only parent-process sampling of existing guest/host counters and Linux thread CPU accounting replaces GDB during timing. NVIDIA utilization is sampled concurrently. Each completed launch supplies three 20-second windows after 10 seconds settling; independent launches, not individual windows, will determine across-run variance.
- Preserved the baseline runtime separately. No runtime, renderer, guest code, timing or pacing changes applied yet. Prepared a standalone wait-semantics/CPU regression test for the finite WaitMultiple fractional-sleep issue.
- First launcher trial debug-a exited with code 0 before the readiness marker was consumed (zero measured windows). Retained its logs and excluded it from performance comparisons. Replacement debug-b launched using the same seed and unchanged binaries; waiting for manual gameplay readiness.
- Debug-b completed all three stationary gameplay windows after user readiness: 25.648 / 25.498 / 25.798 guest submissions/s at 1280x720. Main guest CPU ~99.5% of one core; audio ~72.6%; Xenos commands ~22.8%. Concurrent GPU busy 79.9 / 62.2 / 71.1%, with host presents 447.8 / 248.0 / 357.4 per second. These windows belong to one launch and are not yet across-run reproducibility evidence. User notified capture is complete and asked to exit normally.
- Standalone baseline wait regression test compiles and passes event ordering/reset/consumption, wait-all atomicity, semaphore and APC checks. A 300 ms idle alertable multi-object wait consumes 299.940 ms thread CPU (99.977% of one core), confirming busy polling independently of rendering. A three-line replacement using sleep_until(min(deadline, now+1ms)) is prepared as patches/rexglue-posix-wait-fraction.patch but not yet applied.
- Debug-b subsequently aborted (exit -6), after all timed windows: runtime torchlight_053.log records GPU Commands failed Vulkan command-buffer submission at 22:20:19.377; console reports Graphics device lost. This is the unchanged Debug executable and preserved baseline runtime, so device-loss instability is not exclusive to RelWithDebInfo. No debugger was attached; no fatal native stack/core was captured. Kernel journal around the event contains no NVIDIA fault report. Do not infer that the benchmark completed a stable gameplay/exit trial. Optimized-a launched with the same original runtime for comparison; shader/renderer binaries remain unchanged.
- Applied the narrow finite-wait sleep_until replacement to SDK source after reproducing baseline audio CPU waste. Building only rexruntime and staging a separate candidate library; no game run has used the fix yet.
- Optimized-a completed three windows (42.246 / 39.897 / 44.446 guest FPS) and exited code 0. User reports approximately 10–15 FPS visible improvement. Optimized-b repeated from the identical save/cache seed: 44.846 / 41.596 / 39.796 FPS, mean 42.079 versus 42.196 in optimized-a. Both use unchanged baseline runtime; no audio fix active. Across these two optimized launches mean guest FPS is 42.138, sample variance 0.00681 (FPS²), standard deviation 0.08252 FPS. This does not clear the separately recorded device-loss correctness blocker.
- Candidate runtime build passed (only pre-existing signedness warning at threading_posix.cpp:788). Final standalone tests include finite and infinite delayed signaling as well as event/semaphore/APC/timeout semantics. Three baseline idle 300 ms waits consume 99.993 / 99.991 / 99.994% of one core; three fixed waits consume 1.210 / 1.063 / 1.049%, with wall durations ~300.0 ms and all semantics checks passing. Baseline runtime SHA256 9e74ca7e...; separate fixed candidate 5d0cd9a3.... Installed SDK remains original until gameplay/audio validation.
- Optimized-b exited normally (code 0). Debug-c then completed the counterbalanced baseline sequence Debug → optimized → optimized → Debug: 23.997 / 24.248 / 24.698 guest FPS, mean 24.314. Across two launches per build, Debug mean 24.981 FPS (sample variance 0.88905 FPS²), optimized 42.138 FPS (variance 0.00681 FPS²): 1.687x, approximately +68.7%. All windows report 1280x720. No baseline launch uses the wait fix. Two optimized clean exits do not clear the previously observed optimized failures or new post-capture Debug device-loss abort.
- Debug-c exited normally (code 0). Fixed-a uses the identical Debug executable and Xenos plugin plus the isolated candidate runtime, verified in /proc mappings. Three gameplay windows: 24.098 / 23.198 / 24.748 guest FPS, mean 24.014. Audio worker CPU is 1.800% of one core versus baseline mean 72.677%; main CPU remains 99.44%, Xenos commands 22.51%, GPU busy 78.26%, framebuffer 1280x720. This confirms the audio CPU reduction in real gameplay, but no guest-FPS speedup is demonstrated. User asked to check gameplay/audio/F12 and exit normally; independent fixed repeat pending.
- User confirmed normal gameplay, audio and F12 in fixed-a, followed by normal exit code 0. Fixed-b completed independent repeated windows: 22.748 / 22.998 / 25.698 guest FPS. Across the two fixed launches: guest 23.914 FPS (sample variance 0.02000), audio CPU 1.7748% (variance 0.001250), main CPU 99.4656%, Xenos CPU 22.0479%, GPU busy 78.6206%, host presents 438.617/s. Audio CPU is reduced by 97.56% relative to unchanged Debug. Guest FPS is 4.27% below the two-run Debug mean, so neither FPS improvement nor zero FPS impact is claimed. Manual scene matching, differing host/GPU load and the small sample prevent assigning this modest rate difference to the wait fix. Full per-window and across-launch means/variances are retained in performance-controlled/summary.json.
- Fixed-b: user confirms gameplay/audio/F12 all normal, followed by process exit 0. Intel correctness-only check (fixed-intel, torchlight_057.log) selects Intel UHD Graphics 630 (CFL GT2) from actual Vulkan properties. User confirms controller/gameplay, audio and F12 checks; process exits 0. No timed Intel FPS claim. Both NVIDIA fixed trials and Intel therefore pass manual correctness/normal-exit validation.
- Installed only the tested fixed librexruntime.so into the SDK library directory; final SHA256 5d0cd9a3edf983e04d7185f9bda612d911f44a01defa07ec8932646b95a6ee10 matches the separately tested candidate. Original remains preserved in performance-controlled/baseline-lib. Installed-runtime semantics/CPU check passes again (300.005 ms wall, 2.768 ms CPU, 0.923% of one core). Reverse-application check of the durable patch passes, proving it matches the SDK source change. Installed and both application-local Xenos plugin hashes remain f7cca3412c33e56197c0eef5ca9b194e88cad616cd8671f06869d0c80a12ef7c.
- Completed this focused pass: repeated build comparison supports +68.7% guest throughput from RelWithDebInfo, but it stays experimental due unresolved Vulkan device-loss/black-startup correctness. The new untraced Debug device loss is preserved separately. The narrow wait fix removes ~97.6% of audio-worker CPU cost with passing semantic and manual correctness tests; it does not demonstrate FPS improvement (23.914 fixed versus 24.981 baseline, -4.27% in this small sample), and no zero FPS-impact claim is made. No speculative GPU, guest-copy, pacing, resolution, UI, license or broad runtime changes. Full means, sample variances, methods and limitations are documented in docs/performance-profile.md.

## Standalone Vulkan device-loss investigation — 2026-09-13

- Scope limited to Vulkan correctness. Existing Debug binary, installed wait-fixed runtime and stencil-fixed Xenos plugin retained; no performance, guest-code, input, timing or renderer edits. Loaded Khronos validation on the explicit NVIDIA ICD and requested synchronization checks via a layer settings file. A separate debug-type object was loaded into GDB only, because installed Release SDK libraries have no DWARF locals/types. Initial informational callback stop during debugger setup is not a Vulkan failure.
- Fresh first validation error: VUID-vkQueueSubmit-pSignalSemaphores-00067. Paint submission 130 selects slot 1 and tries signaling semaphore 0x100000000010 for acquired swapchain image 0, while its earlier presentation used image 2, which has not been reacquired. Queue 0x55555d679740, swapchain 0x2570000000257. Both graphics and present family are 0. Paint tracker has completed submission 129, no pending fences, acquired fence 0xe100000000e1. Prior fence status, pool reset and command-buffer end all returned VK_SUCCESS; the invalid submit is paused inside validation and has no return result yet.
- Read-only 96-call Vulkan history captures the prior image-2 present, intervening image-0/image-1 presents, and the exact invalid submit (acquire semaphore 0xf000000000f, COLOR_ATTACHMENT_OUTPUT wait, command pool 0x110000000011, command buffer 0x55555dbdbcb0). Native/all-thread stacks and Xenos state preserved: device_lost=false, no guest GPU submissions/fences, frame_current=1; GPU Commands thread idle, main guest still in startup code. The black window reported by the user was the paused startup capture, not evidence of a new uncontrolled black-startup hang. Diagnostic inferior explicitly terminated after saving evidence; no normal-exit claim.
- Confirmed root of this synchronization violation: present-wait semaphores belong to the three-slot draw-submission ring, and reuse is gated only by draw fences. Draw completion does not establish presentation consumption of the semaphore. This matches the same VUID recorded before the audio fix. It is a concrete device-loss candidate, but causation of the earlier VK_ERROR_DEVICE_LOST aborts is not yet proven. Stopped here as requested, without continuing through invalid submits or applying a patch.
- Narrow proposal: allocate present-wait semaphores per actual swapchain image and select by acquired image index; retain existing per-submission acquire semaphores/command buffers/fences. Associate semaphore retirement with the swapchain generation and establish presentation completion before reuse/destruction; draw fences alone are insufficient there too. Explicit presentation-fence extension support remains to be checked. No renderer rewrite proposed. Full evidence, limitations and fix scope: docs/vulkan-device-loss.md. Artifacts: docs/bringup-artifacts/vulkan-device-loss/{gdb.log,api-history.json,runtime.log,process-maps.txt,capture.gdb,vk_layer_settings.txt,binary-hashes.json,types*}.

## Present-semaphore lifetime implementation — 2026-09-13

- User authorized the narrow fix and repeated NVIDIA/Intel validation. Both actual drivers advertise VK_EXT_swapchain_maintenance1 with swapchainMaintenance1=true, plus its surface-extension dependencies (vulkaninfo artifacts retained). Implemented per-swapchain-image present semaphores selected by acquired image index; existing draw command buffers/acquire semaphores/draw fences remain on their three-slot ring.
- Added a separate instance of the existing VulkanSubmissionTracker for present fences, chained through VkSwapchainPresentFenceInfoEXT. Reclamation polls completed fences; no per-frame host wait or pacing change added. Retirement awaits both draw and presentation resource release before destroying per-image semaphores/swapchain. Allocation-failed presents are dropped from tracking; out-of-date/surface-lost presents retain tracking because their queue operations still enqueue. Missing fence allocation prevents an untracked present; failure to establish retirement completion is terminal rather than freeing live objects.
- Enabled the maintenance feature and its instance dependencies with support checks. The presenter requires the feature for safe retirement; both target GPUs support it. No unproven queue-idle fallback introduced. No shaders, guest GPU/Xenos command processing, POSIX wait or stencil-transfer source changes. Durable patch patches/rexglue-present-semaphore-lifetime.patch covers six Vulkan UI/feature-negotiation files. SDK runtime/plugin rebuild passes; plugin recompilation is required for matching public-header layouts, not changed command-processing logic. Staged matching SDK headers/libraries and rebuilding application-native consumers before validation.
- Debug rebuild passed (native overlay consumer rebuilt for SDK header compatibility; codegen reports module up to date, no generated edits). Fixed runtime 3ce56bb1..., Debug executable 4cd8dca3...; rebuilt/staged Xenos plugin remains byte-identical f7cca341.... First fixed NVIDIA Debug validation run launched under GDB, runtime torchlight_059.log, with per-API tracing disabled and first-error/fatal capture armed. Runtime confirms NVIDIA physical device, swapchainMaintenance1 and Khronos validation. User instructed to play briefly, check controller/minimap/F12 and window resize if practical, then exit normally without a capture wait; manual result pending.
- NVIDIA Debug validation 1 completed: user confirms gameplay/controller, minimap ON/OFF, F12 and window checks all pass; GDB confirms normal exit. Runtime span 217.3s, 7 swapchain creations (including initial creation); no Vulkan validation errors, semaphore/fence lifetime VUIDs or device-loss callback. Complete runtime/GDB logs and result.json retained in present-semaphore-fix/nvidia-debug-1. A single successful run does not clear historical device-loss instability; independent repeats and Intel remain pending.
- User reports markedly slower gameplay during the first fixed validation/GDB Debug run. Diagnostic overhead is a candidate, not yet a measured explanation; do not claim no performance regression. Second NVIDIA diagnostic run is open (torchlight_060.log) for a brief independent lifecycle check. Next compare the same fixed Debug build without GDB/validation before attributing slowdown to the semaphore fix; no timing/pacing or performance optimization changes authorized in this correctness pass.
- NVIDIA Debug validation 2 completed: user confirms checks pass and normal exit; GDB independently confirms normal exit. Runtime span 126.0s, 5 swapchain creations, no Vulkan errors or semaphore/fence lifetime VUIDs, no device-loss callback. Guest FPS was not reported, so no diagnostic FPS estimate is invented. Rechecked existing wait-loop semantics against the newly installed runtime: PASS, 300.007 ms wall / 3.638 ms CPU (1.212% of one core). Same-build untraced NVIDIA comparison is next to investigate reported diagnostic slowdown.
- User subsequently reports approximately 20 Guest FPS for the second diagnostic run. This is a manual overlay reading, not a stationary timed average. Untraced comparison launched with the same fixed Debug executable/runtime and explicit NVIDIA ICD, using the existing read-only observer (present-fix-untraced-debug, PID 78488). Awaiting manual pond readiness before choosing a gameplay measurement window.
- Same-build untraced NVIDIA run completed three 20-second windows: 23.448 / 24.798 / 20.098 Guest FPS at 1280x720 (mean 22.781, within-run sample SD 2.420). User reports it feels normal; observer confirms exit 0. Last window main-thread CPU falls to 82.54% versus ~99.5% in the first two, so the low final sample is retained with that workload caveat. Historical wait-fixed Debug mean was 23.914 FPS; this one-run mean is ~4.7% lower, not a controlled pre/post semaphore-fix benchmark. No clear large regression is reproduced without diagnostics, but zero cost is not proven. Full artifacts in performance-controlled/present-fix-untraced-debug; no timing/pacing changes.
- User explicitly requested skipping Intel validation from now on. Interrupted Intel launch did not start (no runtime/GDB capture or live Torchlight process). Intel support was queried earlier, but no fixed-build Intel gameplay/lifetime validation is claimed. Remaining scope is NVIDIA only.
- Final NVIDIA RelWithDebInfo validation reproduces VK_ERROR_DEVICE_LOST despite zero preceding VUIDs/sync hazards. User reports gameplay, menu/quit attempts and attempts to hide F12 before freezing; causality of those actions is not established. GDB stops at GraphicsSystem::OnHostGpuLossFromAnyThread on GPU Commands thread 113240, from VulkanCommandProcessor::EndSubmission. Runtime torchlight_006.log records failed submission at 18:30:36.416, 143.276s after startup; five swapchain creations. Source/disassembly and preserved R12D=-4 prove vkQueueSubmit returned device lost.
- Recovered failed submit packet from verified caller-stack offsets: queue 0x555559ecbf30/family0, one command buffer 0x7fff73e09570 (pool0x2940000000294), fence0x2920000000292, zero semaphore waits/signals. Xenos submission7695, completed7693, one earlier pending fence; frame2627, device_lost=true. Presenter draw/present trackers each have prior submission36682 pending, with present semaphores still owned per swapchain image. Guest main thread is again at sub_821A5C10 progress comparison (r9=4, r11=6; LR0x821A5CA8), not an InvalidFunctionTrap. Main UI is in a driver ioctl below painting, not shutdown/overlay-toggle code.
- Saved full/native and all-thread stacks, registers/disassembly, failed packet, guest-context binary, process maps/hashes, runtime and kernel logs in present-semaphore-fix/nvidia-optimized-1. Kernel interval has no NVIDIA fault/Xid record. Per-API tracing was disabled, so the underlying GPU command causing device loss is not established by this return site. Process remains paused in GDB (PID112959, session38645); no clean exit or actual abort-signal claim. Stop further renderer changes at this remaining blocker. The semaphore violation is absent in all three NVIDIA validation trials, but the historical device-loss problem is demonstrably NOT fixed. Intel validation remains skipped per user instruction; stencil, POSIX wait, F12 and guest sources unchanged.
- Continued read-only inspection of the original paused PID112959, without continue/kill/restart or inferior function calls. Recovered complete failed deferred stream (101112 bytes, 1337 commands), additional debugger-only type metadata, preceding successful Xenos7694 pool/buffer/fence, current presenter36683 submit/image0 and detailed ownership/layout/descriptor/upload records. No overlap found between pending7694 and failed7695 command/fence objects; five CP resource-retirement queues and pipeline retirement queue empty. 237 bound transient descriptors tagged current frame2627; no overlap with recovered constants/single-buffer free lists. Texture free-list history/driver descriptor contents unavailable. Full records retained under nvidia-optimized-1/paused-inspection.
- New strongest concrete synchronization defect: VulkanSharedMemory::GetUsageMasks(kComputeWrite) returns SHADER_READ (0x20), omitting SHADER_WRITE. Confirmed in unchanged loaded plugin disassembly and failed-stream barrier1328 before full32bpp resolve dispatch1329. Current write range0x1f360000..0x1f6e4000. Source uses this mask for outgoing dependencies too, potentially failing to make compute writes available. No patch applied. Incoming read→write execution ordering alone does not prove a hazard; no later shared-buffer consumer is retained in this stream, preceding stream and GPU execution marker absent. Thus root cause of device loss remains unproven despite a specific mask defect.
- Updated docs/vulkan-device-loss.md with confirmed vs limited exclusions, ranked hypotheses, exact final EDRAM dump/resolve/clear operations, pipeline/shader identities, and smallest next instrumentation proposal: bounded submit/Usage/resolve-descriptor journal, optionally NVIDIA checkpoint markers to locate executed work. Diagnostic extensions are supported but not enabled on this paused device. No F12/shutdown/stencil/POSIX/generated-code or renderer changes; process remains paused.
- Final binary identity recheck: installed runtime3ce56bb1... and application-local Xenosf7cca341... match the original paused capture. No Debug/optimized plugin mixing occurred. Remaining descriptor coverage clarified: 237 current-frame transient bindings, two independently identified persistent EDRAM/shared-memory bindings, three transfer/dump bindings with allocator decoding incomplete.

## Single compute-write mask experiment — 2026-09-13

- User authorized exactly one experiment: retain SHADER_READ and add SHADER_WRITE in VulkanSharedMemory::GetUsageMasks(kComputeWrite), no other renderer/synchronization edits. Durable patch patches/rexglue-vulkan-compute-write-mask.patch is one source line. Original paused evidence is fully retained; diagnostic PID112959 was terminated solely to replace it for the authorized single reproduction. Rebuilding the same SDK Release plugin and RelWithDebInfo NVIDIA application configuration, with existing validation settings. No journal/checkpoints/Intel tests.

## Focused character/inventory capture — 2026-09-14

- User reports Select opens a screen where only the 3D character is visible; the other inventory page renders correctly. BTN_SELECT/code314 is the trigger, not Start. Scope is NVIDIA only; preserve present-semaphore, shader-write visibility, stencil-transfer, wait-loop and shutdown work. No device-loss investigation unless it recurs. Prior shader-cache clearing and stencil removal did not fix this separate UI issue, per user report.
- Prepared opt-in preload observer tools/inventory_capture.cpp and minimal SDK observation patch patches/rexglue-inventory-observation.patch. Original draw body executes unchanged inside a result-recording wrapper; submit observation runs after vkQueueSubmit. Separate read-only evdev descriptor observes BTN_SELECT press without grabbing/consuming application input. Retains three preceding Xenos frames and a bounded 5.5-second post-event selection (first frames plus spaced samples for comparison with the working inventory page), with 128MiB payload cap. Captures counters/timestamps, complete selected deferred command streams, per-draw guest registers/shader IDs, returned result, host pipeline/depth/stencil/blend/color-mask state, viewport/scissor and target identities. No capture journal/checkpoints for historical device loss added. Build/capture validation pending.
- Capture build and preload compile passed. One NVIDIA run completed; physical Select314 marker: host12761/guest756, latest Xenos frame756/submission2510. User confirms broken initial page followed roughly two seconds later by the correctly rendering next page; process exited normally. Runtime confirms selected NVIDIA GTX1050Ti Max-Q, no GPU errors/device loss. Capture hit its128MiB payload cap at about+3.1s:13 frames,47 complete submissions, final frame897 partial. Do not mistake truncation for dropped draws.
- First extra inventory workload is frame759, about67ms after Select. Identified screen-space VSb66e390297113d20/PS7abdfefbcadb4bf9 batch grows26→117 draws; frame826 has126 requests and126 actual Vulkan draws. All submit results VK_SUCCESS. Frame873 has89 such draws using the same actual pipeline/color attachment, full1280x720 viewport/scissor, RGBA writes, ALWAYS depth compare, stencil disabled and SRC_ALPHA/ONE_MINUS_SRC_ALPHA blend. Character viewport is restored for this batch. No concrete incorrect state proven; texture/vertex/pixel contents and later composition remain unresolved. No renderer fix made. Stop after this first focused capture; next minimal step is a replayable frame per page targeting this exact batch's texture/alpha, post-VS output and pre/post-resolve color pixels. Full limitations and artifacts documented in docs/inventory-ui.md; offline decoding/analysis tools added. Existing stencil/compute-write/presenter/wait fixes preserved, no Intel test.

## Inventory pixel replay — 2026-09-14

- Continuing from first focused capture, no broad register dump. Prepared tools/inventory_renderdoc.cpp: observes physical BTN_SELECT314 through read-only evdev, starts one complete guest-frame capture at IssueDraw after XE_SWAP, ends at the next such boundary after IssueSwap/EndSubmission. First capture settles one second after Select; second is manually armed only after the user reaches the working page. Reuses installed RenderDoc1.46 and existing weak callbacks. Capture-only X11 instance gate re-enabled with backups of exact installed runtime/plugin; preserve all renderer fixes.
- First diagnostic launch reproduced the bug on X11, but EndFrameCapture returned0 because Vulkan capture layer was not enabled (RenderDoc API alone was loaded). No RDC produced; correcting only launch settings to explicitly enable VK_LAYER_RENDERDOC_Capture. No renderer inference drawn from this failed capture.
- Corrected NVIDIA capture saved one full guest frame per page (swap sequences1053→1054 broken,3955→3956 working). User confirms initial character-only bug and correctly rendering next/pet page; normal exit. Restored exact pre-capture SDK instance source and installed runtime/plugin, including app-local plugin; byte comparisons all pass. No broad register dump or renderer fix.
- First proven pixel divergence: broken RenderDoc event6109 versus working6408, corresponding six-vertex screen-space quad at870..915,616..661. Post-VS mesh/UV/RGBA(1,1,1,1), BC2 atlas3851/view3859, sampler3850 and VS/PS disassembly are identical. Working pixel(892,638) changes(23,31,40,255)→(8,4,0,255); broken unchanged across the draw, before any final resolve. PixelHistory marks broken event6109 primitive1 shaderDiscarded=True with no culling/depth/stencil/scissor/sample-mask rejection. Thus shader discard before color write, not correct quad pixels later lost in final composition. Exact Kill predicate/system constant remains unproven; stop here per user. No speculative patch. Full A–F evidence in docs/inventory-ui.md and inventory-pixels artifacts; next narrow step if resumed is compare bound XeSystemConstants/debug this exact discarded fragment from the saved captures.
- Continued using only the existing RDC files. Bound SystemConstants alpha/sample fields match:flags0x48c00→GREATER, reference5/255, alpha_to_mask0. Exact cause found in SPIR-V texture translation: result exponent incorrectly uses signed bits13:18 of fetch word4 (LOD bias), instead of word3. Captured word4 differs0x003e0003/0x00000003 (valid LOD−1/0), producing spurious result exponent−16/0; correct word3 is0x00a80d10 with exponent0 in both. Broken alpha1→1/65536 fails alpha>5/255; working alpha1 passes. SDK Xenos layout and D3D translator independently confirm word3 semantic. OriginD, shader translation; not bad alpha state or proven stale binding.
- A single-operand SPIR-V replay correction preserves every Kill/alpha-test/coverage instruction and fixes event6109: shaderDiscardedFalse, pixel(8,4,0,255), full inventory visible in guest target after6497. Corrected working6408 post-draw RGBA image is byte-identical to original. RenderDoc debugger cannot step RoundingModeRTE shaders; exact dataflow traced from captured constants/SPIR-V and corroborated by GPU replay, no fabricated debugger trace. Final replay host-present image does not reflect the modified guest result, so this is not live-window validation.
- Applied smallest generic translator fix via patches/rexglue-vulkan-texture-exponent-word.patch; SDK Release rexruntime/rexgpu-xenos build passed, installed binaries and matching app-local plugin staged. No generated-code, stencil, wait, synchronization, alpha/discard policy, Intel, or live-game/capture changes. Cache loader retranslates stored microcode; no cache purge needed. docs/inventory-ui.md records exact predicates/constants, numerical proof, native build, hashes, and replay/live-output distinction.

## Select Character / BTN_SOUTH observation — 2026-09-14

- User requested bounded evidence only around physical BTN_SOUTH304 (A), 250ms before down through1s after release. Added opt-in host forwarding observer and three read-only SDL callbacks; retains raw evdev, exact SDL and guest XInput bytes, keystroke flags, update/frame counters, character-load visibility/focus/action callbacks and owner result fields. No debounce, suppression, guest/generated-code or renderer changes.
- Existing image/disassembly maps character-load initialization to82386D28, visibility823874D8, focus82389D30(+272 index/+268 scroll), action8238A598 and CEGUI event8238A2E0. Forced index0 during opening is distinguished from actual acceptance (owner+6024=2 then hide). Runtime observation remains pending; no cause inferred from static code alone. SDK Release observer runtime build passed and staged with original backed up; application observer build in progress. Details in docs/select-character-input.md.
- Observer application build passed; all forwarding symbols resolve to their strong host wrappers and originals remain separate. NVIDIA run-1 open under GDB, event10 initialA=up, clock/counter metadata recorded. Plugin SHA256 exactly matches the previously staged texture-exponent fix; no renderer binary change. Waiting for manual menu readiness before arming the single bounded press.
