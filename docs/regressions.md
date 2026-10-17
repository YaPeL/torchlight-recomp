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
