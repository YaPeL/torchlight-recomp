# Regressions

Every bug that reached a hand pass or a release, and every one caught only after the change was
taken as done (by a pull request's review or a run before the merge, where the tests had passed):
what happened, which layer should have caught it, and what catches it now (a test, a log line, or a
checklist item of the pre-release passes). A new entry is added when a bug is found that late; the
lesson goes into a test, `AGENTS.md`, `REVIEW.md` or a lint, not only here.

## The town lamps' light missing on Direct3D 11 (fixed in 0338b58)

- **What happened:** after the backend's per-draw cuts (daeaa8b, constants bound only when they
  change), the lamps' light quads in the town's light map drew nothing on Direct3D 11, the
  default renderer on Windows. Its target bind calls `ClearState`, which unbinds the constant
  buffers, and the skipped bind left the quads reading zeros (an alpha test that always fails).
  GL3+ keeps uniforms per program and was not affected.
- **Should have been caught by:** a render test on the Windows runner's Direct3D 11. The cut was
  measured and replayed on Linux (GL3+) only.
- **Caught by:** the Windows hand pass, then the Windows agent's captures against Xenos.
- **Covered now:** `draw_constants_test` (Direct3D 11 on WARP in CI; GL3+ on machines with a GPU,
  as its `opengl33` label keeps it off CI's runners): two draws with the
  same constants and a target switch between them, checked to fail without the fix. The per-draw
  state contract is written at `tl_backend_set_target` (`backend_api.h`).

## The automatic frame rate cap at 60 with any battery (fixed in e1fc330, before the merge)

- **What happened:** the first version of the automatic cap (9891e33) took a machine that has a
  battery as one running on it, so a laptop plugged in was held at 60 instead of following its
  display.
- **Should have been caught by:** a unit test of the rule with "battery present, plugged in".
- **Caught by:** review of the pull request (#7), before the merge.
- **Covered now:** `host_settings_test` ("a laptop plugged in: the display, up to 144"), and
  `power_supply_linux_test` (a battery at its charge limit, "Not charging", is plugged in).

## Battery never detected: the SDK builds SDL without its power subsystem (fixed in 349e615)

- **What happened:** `platform::OnBattery` asked SDL (`SDL_GetPowerInfo`), and the SDK builds SDL
  with `SDL_POWER OFF`, so the answer was "unknown" on every platform and the automatic cap never
  saw a battery. The unit tests passed: they feed the rule its inputs, not the platform's answer.
- **Should have been caught by:** a check of the platform function against the real machine on
  each platform, unplugged and plugged in; and reading the SDK's third-party build flags before
  relying on one of its libraries' subsystems.
- **Caught by:** the render agent's run on the Linux laptop, unplugged, before the merge.
- **Covered now:** each platform reads its own power information (Linux `power_supply_linux.h`
  with its test; Windows `GetSystemPowerStatus`; macOS IOKit). Pre-release checklist: the startup
  line `settings: fps cap N (automatic; display H Hz, plugged in|on battery)` on a laptop of each
  platform, unplugged and plugged in.

## Texture packs in `replacements` crashing on a level change (fixed in d5ae59e)

- **What happened:** the guest loaded every texture of `replacements` at its file's size into the
  console's 512 MB of physical memory, though the native backend draws its own copy. With TNNR
  (up to 8 times per side) a level change exhausted the guest's physical heap and a texture
  creation ended the game (Windows, v0.12.5-beta's pre-rc1 pass). The folder shipped in
  v0.7.6-beta; its validation covered one mine floor, not a level change.
- **Should have been caught by:** the validation of `replacements` with a large pack: several level
  changes, with the guest's memory measured through them.
- **Caught by:** the Windows pre-rc1 pass.
- **Covered now:** `mods_replacements_guest_view_test` (what the guest is not shown); the guest's
  physical heap per level load measured over 50 loads with and without TNNR (no growth, more than
  330 MiB contiguous). Pre-release checklist: a large pack in `replacements` and at least three
  level changes on each platform.

## macOS vsync not following the display (fixed in ffb7560)

- **What happened:** OGRE's swap interval (`NSOpenGLCPSwapInterval`) is ignored on the game's
  layer-backed view, so vsync did not hold presents to the display: 112-120 presented frames a
  second on a 60 Hz display with a cap of 120, and at 60 the presents followed the cap's timer,
  which the owner felt as judder while the frame counter showed a steady 60. In every release
  with a macOS build (from v0.7.6-beta).
- **Should have been caught by:** a check on each platform that vsync holds presents to the
  display: with vsync on and a cap above the refresh, the presented rate equals the refresh.
- **Caught by:** the owner, by feel, in v0.12.5-beta's Mac pass.
- **Covered now:** the startup line `display refresh: presents paced by display N's refresh` on
  macOS. Pre-release checklist: vsync on with a cap above the display's refresh on each platform
  (presented rate = refresh).

## The lamps' light closed as the game's own behaviour, and it was ours (fixed in 0338b58)

- **What happened:** the lamps' light missing on Direct3D 11 was first closed as "the game's own
  behaviour", with no comparison against Xenos. It was the backend's constants lost after a target
  bind (the first entry above; pull request #6). In the same case, other differences the owner had
  taken for bugs (puddles and shadows not drawn at a distance) were the game's own, and confirmed
  against Xenos at the same frames (`docs/ARCHITECTURE.md`, Future improvements). The closure
  happened in the Windows agent's session and was not recorded in the repository; this entry is
  anchored to the fix.
- **Should have been caught by:** review: a difference is classed as ours or the game's only with
  evidence.
- **Covered now:** `REVIEW.md` lists it as a failure: a difference is the game's own only with the
  same frame against Xenos (`--native_live=off`, parallel mode or an F9 reference) or the guest code
  that does it, read and cited.

## `draw_constants_test`'s GL3+ run passing CI without running

- **What happened:** the backend tests' GL3+ runs (label `opengl33`) need a display, which CI's
  Linux runner did not have: they exited 77 and ctest marked them skipped, so CI was green without
  running them; on Windows and macOS they are left out by label.
- **Should have been caught by:** a CI step that fails when a test it relies on is skipped.
- **Covered now:** the Linux job runs them under `xvfb-run` on Mesa's llvmpipe in a step of their
  own (`Test, GL3+`), which fails if any is skipped or not run.

## The SDK's tests failing for weeks, CI green because it never runs them

- **What happened:** five cases of the SDK's `unit_tests` failed from the `0c7b01a` and `bd833a2`
  bases on, and were written off in `patches/README.md` as upstream issues:
  - four `chrono_test` cases on Linux: libstdc++'s `system_clock` cannot hold 1601. This was not
    only a test problem: the guest's `RtlTimeToTimeFields`/`RtlTimeFieldsToTime` go through the
    same conversion, so on Linux they returned wrong dates for zero, pre-1677 and post-2262
    FILETIMEs. Not seen by a player: a run with a probe runtime (boot, menu, a save loaded;
    2026-10-17) made 189 `RtlTimeToTimeFields` calls, all with 2026 file times, and no
    `RtlTimeFieldsToTime` call.
  - `output_stamp_test`'s depfile case on every platform: the test's own literals were wrong.
  Not machine-specific (no time zone, clock or path involved). CI stayed green because it builds
  the SDK without its tests (`build_sdk.sh`, `windows.ps1`) and runs only this repository's ctest.
  Patch 38's bug (kernels before 4.17) was found the same way, only when someone ran the tests
  by hand on a device.
- **Should have been caught by:** a CI job that builds and runs the SDK's tests with the series;
  and no known failure accepted as upstream without a fix or a written skip.
- **Covered now:** patches 39 and 40 fix the five cases, and patch 41 the four that were
  skipped (`BitStream::Write`'s byte order): Linux 1768 of 1768 with none skipped. The
  `SDK tests` workflow runs them on Linux, Windows and macOS when the patches or the dependency
  scripts change: one at a time (in parallel they collided on macOS), and a skip fails it unless
  `tools/deps/sdk_test_skips.txt` lists it for that platform with its reason (two case-variant
  tests on the case-insensitive hosts).

## Known, open: a hang at startup on the Intel laptop, the guest waiting for the GPU forever

- **What happened:** one start in five of a desktop bench series (2026-10-22, i7-8750H's UHD 630,
  Mesa, Linux; `develop` at the time with rekindled's OGRE patch 0024 in the RTShaderLib) never
  left the first screens. The backend precompiled its 118 programs and processed swap 2; from
  then on the guest's main thread waited for the GPU's progress without end: `guest GPU waits:`
  logged ~35 000 steps and 999.8 ms of waiting per second, every 10 s, for four minutes, until
  the run was closed. Frame 3 never came.
- **What it is not:** the SDK of that run did not carry patch 48 (the command processor's spin
  backoff, integrated later), so 48 is not the cause. 0024 is not likely either: it changes
  skinning shaders on the backend, and the backend had finished its precompilation and its last
  frame. The guest's wait is on the command processor's progress, which the backend does not
  feed. So the hang comes before both: the SDK's GPU sync at startup.
- **Not reproduced:** 42 starts in a row of the same two builds on the same machine (22 under gdb,
  20 without it and with perf ready to record) all reached the menu and quit normally.
- **Should be caught by:** a log line that says, at the moment it happens, where every thread is
  and what it waits for, so a single occurrence (on a player's machine too) is enough to find it.
- **Covered now:** a guest GPU wait over 10 s is logged ("guest GPU wait: ... without reaching its
  target", with the waiting thread's host tid and the GPU's progress word at its start and now),
  followed by the SDK's dump of every guest thread's wait (the waiting thread shows as running
  there: it waits in our hook, not in the kernel) and the command processor's state (patch 50,
  `LogThreadDump`), once and then once a minute; quiet while Android's surface is away
  (`hooks/gpu_wait_watch.h`). The desktop
  benches (local scripts) also record a perf profile when a start waits like this before the
  first menu.

## `develop` never drew on Android: the A2 branch was never merged

- **What happened:** the backend's GLES programs (`android/a2-gles`, b197117: the backend's
  programs in GLSL ES, RGBA where BGRA cannot be sampled) and A2's start near 1280x800 (9485d9d)
  stayed on their branch. Every Android APK measured from 2026-10-17 to 2026-10-22 was built from
  branches on top of it, so they drew; `develop` itself compiled its desktop GLSL on GLES, every
  program failed to link ("Link failed because of missing shader") and the screen stayed black,
  at the display's full resolution (render scale 1.69 on the POCO X7 instead of ~1280x800). The
  frame and present counters looked normal: the backend presents whether or not its programs
  linked. Found on 2026-10-22, when a build from `develop` was run on the phone for the first time.
- **Should have been caught by:** CI. Its Android job only builds; nothing ran the backend on a
  GLES render system, so a program that does not link, or an image that is all black, passed.
- **Caught by:** the owner, looking at the phone during a benchmark series.
- **Covered now:** A2's commits are in `develop`. A CI job on OGRE's GLES2 render system over Mesa
  (EGL, llvmpipe) runs the backend's drawing tests, which check pixels, and replays a synthetic
  capture; it fails on a crash, a program that does not compile or link, a black image or one
  that differs from its reference (`ci/gles-replay`). The RTSS's generated programs need the
  game's files and are not covered there. And every measurement names the commit it was built
  from, reachable from `develop`, from a branch with an open pull request or from an `archive/`
  tag (`AGENTS.md`); what was measured from 2026-10-17 to 2026-10-22 is tagged
  `archive/tl-a4-2026-10-22/*`.

## Profiles of the fight taken on the rest mark (2026-10-21/22)

- **What happened:** the POCO X7 profiling scripts (local, `profile-poco*.sh` and the fight5
  census runs) waited for the fight with `adb shell grep -q "perf step 1" LOG`. adb joins its
  arguments into one command line, so the quotes were lost on the device: `grep -q perf step 1 LOG`
  looked for "perf" alone (in the files "step", "1" and the log) and matched the input script's
  rest mark, 20 s before the fight. The 25-second "fight" profiles held about 18 s of rest. That
  affected the render queue split of 2026-10-22, the fight5 wave 1 against wave 10 profile and its
  spawn windows, and five places of docs/perf/2026-10-22-overnight.md (each with a correction
  note). The frame A/Bs, read from each run's log, were not affected.
- **Should have been caught by:** a check that the profile's window lies inside the step it is
  meant to measure, made by the script itself; and reading the scripts' error output ("grep: step:
  No such file or directory" was printed on every run).
- **Caught by:** the agent, reading a run's output while waiting for a series.
- **Covered now:** the scripts (local, not in the repo) quote the remote command whole (`adb shell
  "grep -q 'perf step 1: fight' LOG"`), record the device's time before and after each profile, and
  check the window with `tools/profile_window/profile_window.py` (`profile_window_test`), which
  fails the run and renames the data `.invalid` unless the profile lies entirely inside the fight.
  Checked on the run's real log: the old window comes out 17.7 s before the mark and fails. The
  repo's guard is the check itself: a profiling script protects its run only if it calls it.
