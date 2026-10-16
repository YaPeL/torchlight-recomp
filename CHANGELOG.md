# Changelog

User-facing changes for the release notes, newest first. Developers: add a line under
"Unreleased" for anything a player or tester would notice.

## Unreleased

### Changed

- The frame rate limit now follows your display by default ("Auto" in Options, Video: its
  refresh rate, up to 144; 60 on laptops and the Steam Deck, and on 60 Hz displays). Existing
  settings keep the limit they have: choose Auto in Options to follow the display.
- With vsync on (the default), what you do reaches the screen about one frame sooner: from about
  70 ms to about 52 ms on the development laptop. The frame counter (F3) shows the latency.

## v0.7.6-beta (2026-10-12)

Everything integrated into `develop` since v0.1.0-beta. Player-facing notes first, the technical
details after them.

### Performance

- Higher frame rates, above all in fights and in the town. Most of the gain comes from work the
  native renderer no longer repeats for the Xbox's graphics chip, and from parts of the level that
  are off screen no longer being drawn.
- The frame counter (F3) shows the frame rate, the 1 % low, long frames and a graph. Next to the
  game's own frame rate it shows the frames that reach the screen and the frames dropped when the
  renderer falls behind. The log reports the same.
- Startup no longer spends 150-180 ms checking for OpenGL 3.3 when the game will not use OpenGL
  (Direct3D 11 on Windows).

### Mods and their textures

- Mods made for Torchlight on PC load. Put each mod folder in the `mods` folder next to your saves.
  The order and on/off switches come from `mods.dat`, the same file PC writes. See
  [the mods guide](docs/mods-guide.md).
- New items, monsters and changed game data work. The first start with a new set of mods takes
  longer while the unit table is rebuilt.
- Mods' textures and models show as on PC: a mod's own textures are used, and a mod cannot replace
  a texture or material the game already has, as on PC.
- Your saves are protected:
  - they are backed up whenever the set of mods changes;
  - items from a mod you removed are taken out, with a notice saying what and where the backup is;
  - nothing is saved in a session where a mod's items failed to load.
- The PC achievements for mods (1, 5 and 10 mods) can be earned.
- Large packs load (the Ultimate Torchlight Mod-Pack, 29 mods), and so do mods with PC-compiled
  data, which crashed at startup before (Enhanced Edition).

### The replacements folder

- Texture packs meant to be copied into PC's `Pak.zip` (texture remasters) go in the new
  `replacements` folder next to `mods`. Their files are used instead of the game's at the same
  path. The folder is not a mod: no order, no achievements, nothing in your saves. Empty it to go
  back to the original files.

### The guest's Direct3D work skipped by default

- With the native renderer (the default), the game no longer programs the Xbox's graphics chip for
  draws only the native renderer makes. Images are the same and frames are faster. Checked on Linux
  with OpenGL and on Windows with Direct3D 11. `--native_skip_guest_d3d=false` turns it off.

### macOS

- A build for Macs with Apple Silicon (M1 or later, macOS 13.3 or later), as a `.dmg`. It is not
  signed by Apple; the README says how to open it the first time.

### Achievements

- Every PC achievement can be earned now, including the ones for playing with mods (1, 5 and 10
  mods), so the PC set is no longer marked incomplete: the choice is shown as "PC".

### Fixed

- Linux on Wayland: the game was shown with 16-bit colour (RGB565), which made gradients band.
  The window now uses 8 bits per channel, like the other platforms.
- Saves reach the disk when the game flushes them, so a crash or power loss right after a save
  keeps it.
- A save renamed to another spelling of its name no longer leaves the old file behind on Linux.
- A crash ends the game instead of leaving it hung.
- The log folder stays bounded: at most 50 MB a run and 200 MB in all; the last 10 crash reports
  are kept.
- If the renderer cannot start, the game reports why instead of crashing.

### Known limitations

- A class added by a mod is not offered at character creation: the Xbox creation screen has the
  game's three classes written into it. This waits for the PC interface
  ([mods.md](docs/mods.md), 7l).
- New affixes and new level pieces from mods are not added yet; mods must be unpacked folders;
  mods are chosen by editing `mods.dat`, not from inside the game.
- Android and a launcher are planned for the next iteration.

### Technical details

**Performance** (`docs/performance-profile.md`; stage 2 closed at `35fd025`):

- Reference route by hand at the stage's close, on the development laptop (i7-8750H at its
  thermal limit, no cap, no vsync), with presented frame rates:
  - main menu: 238.3 fps;
  - dungeon, still: 202.5 fps;
  - dungeon, fight: 167.7 fps;
  - town square: 141.7 fps.
- No measurement on that route exists for v0.1.0-beta (the route was set on 2026-10-10), so there
  is no single figure against the beta. Gains measured step by step:
  - the D3D skip on by default: +4-9 % presented;
  - the default code model on Linux: +11 % in the main menu, +4.5 % in the dungeon standing still,
    no change in the fight;
  - bucket culling: about +7 % on the fight's 1 % low.
- Producer (the hooks on the guest's render thread):
  - flat hash maps and per-thread lookup caches;
  - only the shader constants the backend lacks are sent;
  - no allocation for unchanged constants;
  - frames reserved at the last frame's size;
  - per-hook timing off by default (`--native_producer_timing`).
- Bucket culling (`--native_bucket_cull`, on): each StaticGeometry bucket is tested against the
  main camera with up to 16 boxes, one per piece, using the frustum of the current frame.
- The guest's memory copies run as host `memcpy` where that is the same copy
  (`--native_guest_copy`).
- Code generation:
  - the default code model on Linux x86-64;
  - non-volatile registers, reservation, XER, CTR and CR kept in locals, with a test that no hook
    touches them.
- Backend:
  - consumed frames are freed on their own thread;
  - the program's alpha function and WVP constants are looked up once;
  - viewports the guest sets unchanged are left alone;
  - fetches are byte-swapped a word at a time.
- `replay --bench` plays a session as the live backend thread.
- Logged: level load times, frames past 33 and 50 ms and what the guest did in them, and the guest
  copy mode.
- Tried and not taken, documented: a two-stage backend pipeline, backpressure at the swap,
  deterministic time, the device's `SetTexture` skip and the program-call skip (steps c and d).

**Mods** (`docs/mods.md`, `docs/mods-guide.md`):

- The host builds the guest's PC mod manager and registers each mod with the guest's own mod
  system.
- Each mod's folder is its own device (`tlmodNNN:`), found whatever the letter case.
- PC's text files are read as PC reads them: UTF-16, little-endian, lone CRs, and the tolerance
  for malformed files.
- The unit index is rebuilt with the mods' units, cached under a key and mounted as `tlunits:`.
- At every start, with or without mods, the saves are checked against the unit index the game
  loads. Units the player could lose are protected, and the save's mod list is repaired as the
  Xbox writer leaves it.
- PC's resource-group rule:
  - each mod in its own group, initialised after the game's in `mods.dat` order;
  - a duplicate mod material dropped;
  - textures looked up in their material's group first.
- Mod textures on the host: `mods::TextureResolver` picks the file the guest's OGRE opens,
  including device-named textures. Captures (TLCAP 1.7, chunk `TXFL`) and session recordings carry
  the bytes of mod and replacement files.
- A diagnostics check (`--mods_texture_check`) compares the host's choice with the file the game
  opened.

**Replacements folder**: mounted as `tlrepl:` and added last to the pak's resource groups (`0ZIP0`,
`ZIP`).

**D3D skip** (`docs/native-skip-guest-d3d.md`; `--native_skip_guest_d3d`, on by default since
`a859bb5`):

- What is skipped:
  - the device-only sampler state slots;
  - the class A render states;
  - `_setTextureMatrix`;
  - the device's draws and index/stream bindings.
- Validation: replays compared byte for byte, and A/B captures on Windows with Direct3D 11 (no
  state differences).

**macOS**:

- The platform module for macOS. OGRE's window is created, resized and destroyed on the main
  thread.
- OGRE 14.6.0 for arm64, and the SDK's mac-arm64 preset.
- The app bundle, `.dmg`, checks and symbol split (`packaging/macos/`).
- Durable writes with `F_FULLFSYNC`.

**Tools and CI**:

- CI runs on pushes and pull requests into `develop` and `main`, on Linux, Windows and macOS. The
  prebuilt SDK and OGRE are kept as assets of the `deps` prerelease, keyed per platform.
- The release builds the AppImage, the Windows zip and the macOS `.dmg`, from tags on `main` only.
- `tools/run_capped`: game runs with a time limit, a log size limit and nothing left behind.
- Development input scripts (`--dev_input_script`, development builds only).
- `tools/achievement_coverage` checks the PC achievements' coverage summary against its table.
- SDK patches 20-31 in `patches/series`, rebased on ReXGlue `bd833a2`:
  - DOS wildcards in directory listings;
  - SDL's software renderer;
  - fctiw/fctid rounding, ARM64 `mffs`, `mtfsf`;
  - log rotation;
  - guest file flushes reach the disk;
  - missing files logged at debug level;
  - case variants removed on rename;
  - fatal faults and guest errors end the process.

## v0.1.0-beta (2026-10-06)

First public beta for Linux and Windows.
