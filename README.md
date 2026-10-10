# Torchlight Recomp

[![CI](https://github.com/YaPeL/torchlight-recomp/actions/workflows/ci.yml/badge.svg)](https://github.com/YaPeL/torchlight-recomp/actions/workflows/ci.yml)
[![Release](https://github.com/YaPeL/torchlight-recomp/actions/workflows/release.yml/badge.svg)](https://github.com/YaPeL/torchlight-recomp/actions/workflows/release.yml)
[![Latest release](https://img.shields.io/github/v/release/YaPeL/torchlight-recomp?include_prereleases&filter=v*&sort=semver)](https://github.com/YaPeL/torchlight-recomp/releases)
[![License: GPL-3.0](https://img.shields.io/badge/license-GPL--3.0-blue)](LICENSE)
[![Platforms: Linux | Windows](https://img.shields.io/badge/platforms-Linux%20%7C%20Windows-lightgrey)](#installing)

An unofficial native PC port of Torchlight for Xbox LIVE Arcade (Xbox 360), made by static
recompilation with the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk). The game's PowerPC
code is translated to C++ and compiled for x86-64; instead of emulating the Xbox 360 GPU, every draw
is rebuilt with [OGRE 14](https://www.ogre3d.org/) on OpenGL 3.3 (or Direct3D 11 on Windows).

> [!IMPORTANT]
> The releases contain no game assets and no original game files. The executable is built from the
> game's code and only runs with files from your own copy of Torchlight for Xbox LIVE Arcade (the
> XBLA package from your Xbox 360, version 1.0.140.0). This project is not affiliated with or
> endorsed by the owners of Torchlight or by Microsoft.

This is a **beta**, the first public release. See [Known limitations](#known-limitations).

## Contents

- [Installing](#installing)
- [First start](#first-start)
- [What works](#what-works)
- [Known limitations](#known-limitations)
- [Where files are kept](#where-files-are-kept)
- [Building](#building)
- [Credits](#credits)
- [License](#license)

## Installing

Download only from this repository's
[Releases](https://github.com/YaPeL/torchlight-recomp/releases) page. Each release has a
`SHA256SUMS` file with the checksum of every download.

### Linux

Requirements: x86-64, glibc 2.35 or newer (Ubuntu 22.04 or later, or any current distribution), a
GPU driver with OpenGL 3.3. X11 and Wayland both work.

```sh
chmod +x Torchlight-Recomp-*-x86_64.AppImage
./Torchlight-Recomp-*-x86_64.AppImage
```

The AppImage carries everything it needs except the system's graphics and sound libraries.

### Windows

Requirements: 64-bit Windows 10 or 11. Direct3D 11 is the default and recommended renderer: it
uses less CPU than OpenGL. OpenGL 3+ can be chosen in the settings when the GPU driver provides
OpenGL 3.3.

Extract `Torchlight-Recomp-*-x86_64.zip` anywhere and run `TorchlightRecomp\torchlight.exe`.

The zip is not code-signed. The first time, Windows SmartScreen may show "Windows protected your
PC": that is Windows being cautious with programs it has not seen downloaded often, not a detection
of anything harmful. Click **More info**, then **Run anyway**; Windows remembers the choice for
that copy. To check a download, compare its SHA-256 with the release's `SHA256SUMS`.

## First start

1. A dialog asks for your Torchlight XBLA package: the file copied from your console's storage, or
   a folder with its contents already extracted. The package is checked (it must be Torchlight's,
   version 1.0.140.0, with every file intact) and its files, about 200 MB, are copied to the game's
   data folder ([Where files are kept](#where-files-are-kept)). Your package is only read.
2. A second dialog asks which achievement set to use, Xbox 360 or PC
   ([Achievements](#achievements)). It can be changed later in the settings.

After that the game starts; later starts go straight to it. These dialogs follow your system's
language (English, German, French or Spanish).

A gamepad is recommended. A keyboard can play through ReXGlue's controller emulation: start the game
with `--mnk_mode=true`.

## What works

The game runs on the native renderer: menus, town, dungeons, character creation, inventory, pets and
saves.

On a laptop with a GeForce GTX 1050 Ti (Linux, 720p, no frame cap), the native renderer runs the
game about twice as fast as the same build with ReXGlue's emulated Xenos GPU (1.9–2.5 times the
frame rate), loads levels 1.5–2 times faster, and has almost no frames over 33 ms
([measurements](docs/performance-profile.md#native-renderer-against-xenos-2026-10-07)).

### Display settings

The game's own Options → Settings menu has an extra **Video** column:

- **Resolution**: the internal render resolution, independent of the window's size.
- **Aspect ratio**: Auto (your display's), or 4:3, 16:10, 16:9, 21:9 and 32:9 up to your display's
  width. A wider ratio shows more of the scene instead of stretching it.
- **Frame rate limit** and **Vertical sync**.
- **Renderer** (Windows: Direct3D 11, recommended, or OpenGL 3+) and **GPU**, on machines with more
  than one.
- **Language** and **Achievements** (below).

Some settings apply after a restart; the column says which.

### Achievements

Two sets, chosen at the first start or in the Video column. Each keeps its own progress.

- **Xbox 360**: the game's original 12 achievements, listed with their icons in the game's
  Achievements screen.
- **PC**: the 66 achievements of the PC version, with an unlock notification in the game. The three
  for mods (`MODS_1`, `MODS_5`, `MODS_10`) are earned by playing with PC mods ([Mods](#mods)). They
  are kept on this machine: there is no Steam connection.

### Importing PC saves

Characters, the shared stash and the options of Torchlight for PC can be brought over. Copy them,
together with the PC version's `Pak.zip`, into the `import` folder of the game's data folder. When
you open the "load character" menu, the game offers the import and explains anything it cannot
convert. Your PC installation is never touched; the copies in `import` are renamed once imported.

### Mods

Mods made for Torchlight on PC can be used: copy their folders into the `mods` folder next to your
saves, and order or turn them off in `mods.dat`, as on PC. Large packs such as the Ultimate
Torchlight Mod-Pack load, and your saves are copied whenever the set of mods changes. See
[docs/mods-guide.md](docs/mods-guide.md) for how to install them, what works and what does not
yet.

### Languages

English, German, French and Spanish, as in the game, chosen in the Video column (after a restart).
Other languages can be added as language packs, including translations made for the PC version: see
[docs/translations.md](docs/translations.md).

## Known limitations

- **Beta.** Expect bugs. Please report them in the
  [issues](https://github.com/YaPeL/torchlight-recomp/issues), with the log of the run
  ([Where files are kept](#where-files-are-kept)).
- **PC achievements** are covered by tests, but only a few have been earned in actual play so far.
- **Xbox LIVE** features (sign-in, leaderboards) are not available.
- **Steam Deck**: the AppImage is meant to work in Desktop Mode but has not been tested on the
  device yet.
- **macOS** is not supported in this beta; a macOS version is planned for the next release.
- **Ultrawide**: at 32:9, the story screens shown inside a level can show a few rows of the level at
  the top right.
- **Mods**: new affixes and new level pieces from mods are not loaded yet, mods must be unpacked
  folders, and the first start with a new set of mods takes longer (up to about a minute with a
  large pack). The details are in [docs/mods-guide.md](docs/mods-guide.md).
- **Languages**: language packs for scripts written right to left (Arabic) are not supported, and
  Chinese and Japanese not yet.
- The Windows zip is not code-signed (see SmartScreen, above).

## Where files are kept

| | Linux | Windows |
|---|---|---|
| Settings (`settings.toml`) | `~/.config/TorchlightRecomp/` | `%APPDATA%\TorchlightRecomp\` |
| Game data, with `import` for PC saves | `~/.local/share/TorchlightRecomp/game/` | `%LOCALAPPDATA%\TorchlightRecomp\game\` |
| Saves | `~/.local/share/TorchlightRecomp/` | `%USERPROFILE%\Saved Games\TorchlightRecomp\` |
| Mods (with `mods.dat`) | `~/.local/share/TorchlightRecomp/mods/` | `%USERPROFILE%\Saved Games\TorchlightRecomp\mods\` |
| Logs | `~/.local/state/TorchlightRecomp/logs/` | `%LOCALAPPDATA%\TorchlightRecomp\logs\` |
| Shader cache | `~/.cache/TorchlightRecomp/` | `%LOCALAPPDATA%\TorchlightRecomp\ogre\` |

On Linux the `XDG_*_HOME` variables are honored.

## Building

Building the game needs your own copy of it too: the code generator reads its executable. Without
it, only the libraries, the tests and the tools build (what CI does). See
[docs/BUILDING.md](docs/BUILDING.md) for the dependencies (the patched ReXGlue SDK, OGRE 14.6.0),
the build on Linux and Windows, the tests and the development tools, and
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for how the port works. To contribute, see
[CONTRIBUTING.md](CONTRIBUTING.md).

## Credits

- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk): the static recompiler and runtime this port
  is built on, which includes code derived from [Xenia](https://github.com/xenia-project/xenia).
  Our patches to it are in [patches/](patches/).
- [OGRE](https://github.com/OGRECave/ogre): the renderer that draws the game.
- [Rayman Origins Recompiled](https://github.com/XDanfr/RaymanOriginsRecomp): the null GPU plugin
  patch ([patches/README.md](patches/README.md)).
- [SDL](https://www.libsdl.org/), [FFmpeg](https://ffmpeg.org/),
  [Dear ImGui](https://github.com/ocornut/imgui), [miniz](https://github.com/richgel999/miniz) and
  the other components listed, with their licenses, in
  [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
- [Unleashed Recompiled](https://github.com/hedge-dev/UnleashedRecomp) and
  [Zelda 64: Recompiled](https://github.com/Zelda64Recomp/Zelda64Recomp), whose release and
  packaging practices this project follows.

Torchlight was made by Runic Games.

## License

- The project's own code: GNU General Public License, version 3 ([LICENSE](LICENSE)).
- The patches to the ReXGlue SDK in [patches/](patches/): BSD 3-Clause License
  ([patches/LICENSE](patches/LICENSE)), ReXGlue's license, so that they can go upstream. Patches
  taken from other projects keep their original license and attribution.
- The game is not included and is not covered by these licenses, nor is the code generated from it
  (`generated/`).
- Third-party components: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
