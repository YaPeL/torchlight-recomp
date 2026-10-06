# Installer and CI without game content (TLR-003)

Research for TLR-004 (install script) and TLR-012 (CI). Requested model: only source code and a
script are distributed; the user provides their own Torchlight XBLA package and the script does
everything on their machine (extract the XEX and data, codegen, build). No executable and nothing
derived from the game is distributed, and CI does not use game files.

Sources consulted on 2026-10-04. Repositories were read at the commit given.

## Summary

- **None of the large reference projects follows the requested model.** Nocturne, Unleashed
  Recompiled and Zelda64Recomp distribute already recompiled executables (they contain the game
  code translated to C++ and compiled), and their CI builds with the XEX/ROM, downloaded from a
  private repository with a secret token. What they do not distribute is the assets or the
  original XEX/ROM.
- The only one that matches the model is **RaymanOriginsRecomp (XDanfr)**: source only, the user
  builds with their own copy, there are no desktop releases and no CI with the game.
- **Trial**: on XBLA the downloaded package is the same for the demo and the full game; what
  changes is the license the console reports (`XamContentGetLicenseMask`). Nocturne sets
  `license_mask = 1` by default and does not verify the purchase in any way. No project verifies
  that the user bought the game: they validate that the files are exactly the expected version
  (hash) and nothing else.
- **Torchlight currently runs in trial mode**: the recomp does not set `license_mask`, the SDK
  default is 0, and the game stores bit 0 of the mask as "full game" (details below). To be
  confirmed in the game (see "Open questions").
- **CI without game files** can only build and test what does not depend on `generated/`:
  `guest_abi`, `commands`, `rtss`, the resource registry, `live` (queue, snapshots, session), the
  backend conventions, `capture_dump`, `replay`. The `torchlight` executable cannot be built in CI
  without the XEX, and the existing `.tlcap` captures cannot be used either (they contain game
  textures and vertices).

## 1. Nocturne (Castlevania: SotN XBLA)

Repo: <https://github.com/birabittoh/NocturneRecomp>, commit `5390d5ec` (2026-08-27). SDK: fork
`birabittoh/rexglue-sdk`, tag `sotn-nightly-20260817-7766f971` (pinned in `.sdk-version`).

### What it distributes

- Host source (`src/`, MIT license), scripts and CI. The `.gitignore` excludes `game/`, `assets/`,
  `generated/`, TU packages and "anything derived from it must NEVER be committed".
- **Executables**: GitHub releases and nightlies as CI artifacts ("Get the latest stable build from
  the Releases page"; `README.md`). The executable contains the recompiled game code. It also shows
  up in a third-party launcher (Goopie).

### Flow from the user's package

Two paths, both with the same hash pin:

1. **Release user** (the main path): downloads the executable, runs it and a wizard asks for the
   game. It is `rex::system::GameDataSelector::EnsureGameData`, which lives in the SDK fork
   (`include/rex/system/game_data_selector.h`, `src/system/game_data_selector.cpp`; it is not in
   the upstream SDK we use). It runs in `SetupEnvironment()`, before any window or presenter is
   created, with native SDL dialogs (message box + file picker). It accepts an ISO, an XBLA package
   (LIVE/CON/PIRS) or an already extracted directory; extracts if needed; validates `default.xex`
   against a fixed SHA-256 and writes `game_data_root` to the config `.toml` so it does not ask
   again. Nocturne configures it like this (`src/nocturnerecomp_app.h:159-168`):
   `default_xex_sha256 = "26a58b07…"`, `is_xbla = true`, and for TU builds also
   `title_update_sha256`.
2. **Build from source** (`README.md`, section "Building from scratch"):
   - Dependencies: clang 20, cmake, ninja, vulkan-headers (Linux); llvm, cmake, ninja (Windows).
     Python for the scripts.
   - `scripts/download-sdk.py --pinned`: downloads the **prebuilt** SDK from the fork release named
     in `.sdk-version` (it does not build the SDK).
   - The user puts their LIVE package in `game/` and runs `scripts/extract_game.py`: an STFS
     extractor of ~200 lines in pure Python (no crypto: the data of a LIVE package is not
     encrypted; the XEX is, but `rexglue codegen` handles that). It only validates the `LIVE` magic
     and that `default.xex` exists at the root.
   - `scripts/build.py`: detects the preset, downloads the SDK if missing, runs `rexglue codegen`
     only if a hash of the inputs changed (XEX, `.xexp`, configs, manifest, SDK version), configures
     and builds with CMake, and copies the SDK `.so`/`.dll` next to the executable. With `--tu` it
     picks the TU that matches the base XEX by digest.
   - The XEX hash check happens at startup (the `GameDataSelector`), not in the script.

### Trial

- Nocturne does nothing specific in the game code: it sets the SDK cvar `license_mask = 1` in its
  defaults (`src/settings.cpp:93`).
- In the SDK, `XamContentGetLicenseMask` returns the value of that cvar (default 0); the comment
  says most games use bit 0 as "purchased" (`~/rexglue-sdk/src/kernel/xam/xam_content.cpp:24-45`).
  It is the same convention as Xenia (the "license_mask" option; see the issue
  <https://github.com/xenia-project/xenia/issues/1360>).
- It does not tell the demo from the full game in the package nor verify the purchase: on XBLA the
  demo and the full game are the same package, and the difference is the console/profile license,
  which the recomp cannot check. The only thing verified is that the XEX is the expected revision.

### CI

`.github/workflows/_build.yml` (called by `ci.yml` on every push/PR and by `release.yml` on tags):

- Matrix Windows x64, Linux x64, Linux arm64.
- **Downloads `default.xex` from a private repo** (`vars.ASSETS_REPO`, token
  `secrets.ASSETS_TOKEN`) and caches it; the TU packages too. If the file is smaller than 1 MiB,
  it fails.
- Builds vanilla and with the TU, packages and uploads artifacts (CI) or creates the release (tags).
- It runs no tests or smoke tests: it is build and packaging.

Conclusion: Nocturne's CI **does use game files**, and without the secret it does not even build.
There is nothing in its CI that can be copied for a CI without the game.

## 2. Secondary references

### Unleashed Recompiled (Sonic Unleashed)

<https://github.com/hedge-dev/UnleashedRecomp>

- Distributes executables (releases, Flatpak). Installer built into the game
  (`UnleashedRecomp/ui/installer_wizard.cpp`, `install/installer.cpp`): accepts STFS containers,
  ISOs or folders, detects by itself what content it is (game, TU, each DLC) and copies/verifies
  every file against XXH3 hash tables generated by its own tool (`install/hashes/*.cpp`, "File
  automatically generated by fshasher"). It rejects modified files ("it is not possible to complete
  the installation if your files have been modified"; `README.md`).
- Building requires copying `default.xex`, `default.xexp` and `shader.ar` into
  `UnleashedRecompLib/private/` (`docs/BUILDING.md`).
- CI (`.github/workflows/validate.yml`): checks out a **private assets repo** with a token and
  copies its contents to `private/` before building. External PRs go through an environment with
  manual approval (`validate-external.yml`, `pull_request_target`) so the secret is not exposed.
- Steam Deck: they recommend installing in desktop mode because the file picker may not be
  available in Game Mode (README FAQ). Relevant for TLR-004 if the installer were graphical.

### Zelda64Recomp (Majora's Mask, N64)

<https://github.com/Zelda64Recomp/Zelda64Recomp>

- Distributes executables. The user picks their ROM from the game menu and the program loads the
  assets from it, with no extraction step (`README.md`). It only accepts the NTSC-U version (to
  build, the decompressed ROM with SHA-1 `d6133ace…`; `BUILDING.md`).
- CI (`validate.yml`): clones a private repo with `secrets.ZRE_REPO_WITH_PAT` before building.

### Rayman Origins

- `XDanfr/RaymanOriginsRecomp` (XenonRecomp + its own runtime; desktop and Android):
  <https://github.com/XDanfr/RaymanOriginsRecomp>. **It is the only one that matches the requested
  model.** Its README: "Everything derived from the game (the executable, the recompiled C++ and
  the game data) is generated or read locally from your own dump and must never be committed,
  uploaded or shared here"; the "Legal" section says the repo only has original code and "facts
  about the executable" (addresses, function boundaries). The user copies their files into
  `private/game/`, runs the tools and builds. The tools only accept the retail `default.xex`
  without a TU (SHA-256 `1444bbea…`). It has no published CI workflows.
- `edeegg/origins-recomp` (ReXGlue, Android): <https://github.com/edeegg/origins-recomp>. The user
  puts the `.xex` in `game/`, but APKs are published as releases. No published CI.

## 3. Torchlight: data for the design

### The user's package

The package on the development machine (`~/recomp/35F62CE3FE43845B1625A000921D5A6C48CCC7C058`; the
name is the content ID, like the file ignored by Nocturne's `.gitignore`):

| Field (offset in the STFS header) | Value |
|---|---|
| Magic (0x000) | `LIVE` |
| Content type (0x344) | `0x000D0000` (Arcade Title) |
| Title ID (0x360) | `0x58410A7E` (the same as in the save path `…/58410A7E/…`) |
| Name (0x411, UTF-16BE) | `Torchlight` |
| Size | 206 135 296 bytes |

Extracted `default.xex`: 5 443 584 bytes, SHA-256
`87e4e1cd7b2eda9a1a6d8607609ecdb05474923ade81440475c2d8c2d55a4b6e`. `ArcadeInfo.xml` declares
`projectVersion="1.0.140.0"`.

Extracted contents (`~/360tools/extracted/extracted`): `default.xex`, `pak.zip`, `music/`,
`programs/`, `rtshaderlib/`, `translations/`, `resources.cfg`, `randombossroom_rules.dat`,
`ArcadeInfo.xml`, achievement/gamerpic/icon images, `AvatarAwards`.

The hash of the whole package is not usable for validation: the STFS header carries the signature
and the license entries of the console/account that downloaded it, so two legitimate copies of the
same game need not match. That is why Nocturne and Unleashed validate the files inside.
(Plausible, not verified with a second package.)

### Trial in Torchlight

- The guest calls `XamContentGetLicenseMask` from a single place: `sub_823AC840` (through the thunk
  `sub_8287E5F0`). If the call succeeds it stores `mask & 1` in the global `0x8355A27C`; otherwise
  it returns the previous value (`generated/default/torchlight_recomp.53.cpp`, function
  `sub_823AC840`).
- `sub_823AC840` is called again from `sub_82194A98` (the notification loop), among other cases on
  notification `0x02000007` (`kXNotificationLiveContentInstalled` in Xenia, `src/xenia/xbox.h:246`
  in xenia-canary: what arrives after buying from the demo). Also from `sub_82206000`.
- The global is read by 12 functions (e.g. `sub_82193228`, which returns early if it is not 1).
- `pak.zip` ships the demo screens: `media/UI/trialupgrade*.layout`,
  `trialachievementupgrade*.layout`, `unlockfullgamemenu*.layout`. Their texts say to unlock the
  full game to go deeper into the mines, and that an achievement was earned but the full game is
  needed to receive it. So the demo limits the depth in the mines and does not award achievements.
- The recomp does not set `license_mask` (there is no `REXCVAR_SET` and no config in `src/`) and the
  SDK default is 0, so the game should currently behave as the demo. This also affects the
  achievements work: in demo mode the game awards no achievements.

Reading of the recompiled code; the game was not run for this research.

## 4. Proposal for Torchlight

### Content boundary

- In the repo: our own code, the script, the game identity constants (title ID, content type,
  SHA-256 of `default.xex` and of the data files), addresses and codegen configuration
  (`config/torchlight_functions.toml`, as today). Same as RaymanOriginsRecomp: facts about the
  executable, not the executable.
- Outside the repo and outside any artifact: the package, everything extracted, `generated/`, the
  built executable, `.tlcap` captures, `.tlses` sessions, saves.
- This is stricter than Nocturne/Unleashed/Zelda64: there is no "release" to download. The cost is
  that the user needs a full toolchain (clang, cmake, ninja, python) and build time; on the Steam
  Deck that means desktop mode and quite a lot of disk.

### Script (TLR-004)

A Python script (standard library only for the package part) with idempotent steps, each with an
actionable error:

1. **Dependencies**: clang ≥ 20, cmake ≥ 3.25, ninja, python ≥ 3.11, git. A per-platform message
   with the install command (like Nocturne's README).
2. **ReXGlue SDK**: clone the pinned upstream commit, apply `patches/*.patch`, build and install
   into a project directory. Unlike Nocturne there is no prebuilt SDK to download: ours carries its
   own patches. Alternative to evaluate: publish a prebuilt patched SDK (it contains nothing from
   the game).
3. **OGRE 14.6.0**: clone the tag and build with the README recipe (today done by hand in
   `~/ogre14-install`; the cross-platform stage in `ARCHITECTURE.md` already asks to describe it
   inside the project).
4. **The user's package**: an argument or the only file in `game/`. Validate in this order, each
   with its own message: `LIVE`/`CON `/`PIRS` magic; content type `0xD0000`; title ID
   `0x58410A7E`; `default.xex` present at the root. Extract with our own STFS reader (Nocturne's is
   MIT and works as a reference; note that it only covers the single-block hash table — the
   Unleashed/ReXGlue one covers more variants). Then: SHA-256 of `default.xex` against `87e4e1cd…`
   and of every data file against a fixed table in the repo. If the XEX does not match: "this
   package is not version 1.0.140.0 of Torchlight XBLA" (and if someone shows up with another
   revision, it is added to the table with its codegen config).
5. **Codegen**: `rexglue codegen torchlight_manifest.toml` with `game_root` pointing at the
   extracted directory, skipping the step if the input hash did not change (same criterion as
   Nocturne's `build.py`).
6. **Build**: `cmake --preset …-release` + build.
7. **Install**: a folder with the executable, the SDK and OGRE `.so`/`.dll`, OGRE plugins/resources
   and a `torchlight.toml` with `game_data_root` (and the license, next point). A launcher
   (`.desktop` on Linux, a shortcut on Windows) is optional.

Trial: the script writes `license_mask = 1` to the config `.toml` it generates, like Nocturne. There
is no technical way to verify the purchase offline (the license is tied to the console/profile) and
no reference project tries. It is left as the project owner's decision (see questions). Important:
the `license_mask` change is already needed today for the game to award achievements and not stop
in the mines.

A graphical wizard like the fork's `GameDataSelector` does not apply: with the "source only" model
the executable does not exist before having the game, so the validation goes in the script. The
executable can still check at startup that `game_data_root/default.xex` has the hash it was built
with (cheap, and it avoids mixing data from another revision).

### CI (TLR-012)

Without game files there is no `generated/`, and today the root `CMakeLists.txt` does
`include(generated/rexglue.cmake)` unconditionally. Proposal:

1. **CMake change** (its own task, not touched here): an option to configure without the game
   executable, building only the libraries and tests that do not depend on `generated/`. Today those
   are: `guest_abi_layout_test`, `commands_test`, `rtss_test`, `resource_registry_test`,
   `measured_mutex_test`, `snapshot_store_test`, `session_file_test`, `frame_queue_test`,
   `conventions_test`, plus `capture_dump` and `replay`. Several link `rex::runtime` or
   `rex::gpu-xenos` (`live_content_source_test`, `detile_test`, `torchlight_platform`): they need the
   built SDK but not the game, so they are in if CI builds the SDK.
2. **Matrix** Linux x64 and Windows x64 (stage 1 of TLR-012): patched SDK (cached by commit + hash of
   `patches/`), OGRE 14.6.0 (cached), build of the "no game" option, `ctest`.
3. **Replay without the game**: the backend can be exercised with synthetic captures generated by a
   test (a few draws with invented textures and vertices, written with the `commands/` serializer).
   Real captures (`out/captures`) cannot go to CI. OGRE GL3+ on a runner without a GPU needs Mesa
   (llvmpipe) and a virtual display or surfaceless EGL; feasible on Linux, harder on Windows.
4. **Script lint**: tests of the STFS reader with a synthetic package (generated by the test, with a
   fake `default.xex`) that check the error messages of every validation step.
5. What **cannot** be verified in CI: codegen, building `torchlight`, startup. That remains a manual
   validation on the machine with the game before every relevant merge.

### Decisions and open questions

1. `license_mask = 1` in the generated config: **approved** (2026-10-04).
2. Confirmed by the user: today the game shows the "Unlock Game" entry in the menu, i.e. it runs as
   the demo.
3. Open: publish a prebuilt patched ReXGlue SDK (with nothing from the game) so the script does not
   have to build it, or always build it on the user's machine.
4. Only one revision of Torchlight XBLA is supported (XEX `87e4e1cd…`); there are no TUs or other
   regions to consider.
