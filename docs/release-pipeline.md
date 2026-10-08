# Release pipeline: research

How other static recompilation projects build and publish binaries with game files that only CI
sees, and a proposal for Torchlight. Research only: no code, CMake or workflow was written.

Project decision (2026-10-05), replacing the "source only" model of `docs/installer-research.md`
(section 4): CI builds the game with the XEX, downloaded from a private repository with a secret,
and the compiled executable is published. At install time the user provides their own game files
(the assets and the XEX the runtime loads). The XEX, the game assets and the generated code are
never published as files or as source.

Scope (2026-10-05): the first release is a **Linux-only beta (AppImage)**. Windows is added once
the port exists (`docs/windows-port.md`); everything Windows-specific below is marked "after the
beta". 2026-10-06: Windows joins the beta (a zip, `docs/windows-port.md`, Windows package); the
CI and release jobs in 5.3 and 5.4 describe it, and some "after the beta" notes below are history.
The decisions taken on the proposal are in section 5.7.

Sources were consulted on 2026-10-05. Repositories were read at the commit given:

| Repository | Commit |
|---|---|
| hedge-dev/UnleashedRecomp | `cf829a9e` (2026-06-29) |
| Zelda64Recomp/Zelda64Recomp (branch `dev`) | `b65c482e` (2026-09-25) |
| birabittoh/NocturneRecomp | `5390d5ec` (2026-08-27) |
| thefixinhixon/hells-gate-recomp | `69a4b770` (2026-09-28) |
| rexglue/rexglue-sdk (local checkout, our base) | `bd833a2` |

## Summary

- **Everyone uses the same pattern**: a private repository holding only what the build needs from
  the game (the XEX, plus a title update or shader cache in some cases), read by CI with a secret
  (a PAT; none uses a deploy key). External PRs either get no secret or go through an environment
  that a maintainer approves.
- **Formats**: Windows is always a **zip** (no installer). Linux varies: Zelda64Recomp ships an
  **AppImage** (built in an Ubuntu 18.04 container with linuxdeploy), Unleashed ships a **Flatpak**,
  Nocturne ships a **tar.gz**. The Dante's Inferno ReXGlue fork ships an AppImage too. None of them
  signs the binaries.
- **What the user provides**: always their own copy, checked by hash, through an installer or
  wizard built into the game (Unleashed, Zelda64Recomp, Nocturne's SDK fork). In ReXGlue the runtime
  also loads `default.xex` at startup (`rex_app.cpp:262-286`), so the user's XEX is required at
  runtime and not only to build.
- **Risk found in a reference**: Nocturne caches `default.xex` with `actions/cache` in its public
  repository. GitHub documents that a pull request, including one from a fork, can restore caches of
  the base branch, so anyone can get the XEX without any secret. In Torchlight, no game-dependent job
  may use the cache (compiler caches included: they hold compiled game code).
- **AppImage for Torchlight**: feasible, but the current build is not relocatable: absolute OGRE
  paths compiled in (plugins and media), RUNPATH pointing at `~/ogre14-install`, SDK libraries
  found only through `LD_LIBRARY_PATH`, the SDK's config and logs next to the executable (read-only
  inside the AppImage), and glibc 2.43 required (the Steam Deck has 2.41). All of these are bounded
  tasks, listed in section 5.
- **Windows**: depends on the port (`docs/windows-port.md`, WIN.1-WIN.5). It does not exist yet, so
  the first release is a Linux-only beta (decided, section 5.7).

## 1. How the reference projects build and publish

### 1.1 Comparison

| | Unleashed Recompiled | Zelda64Recomp | Nocturne (ReXGlue) |
|---|---|---|---|
| Private repo access | `actions/checkout` of `secrets.ASSET_REPO` with `secrets.ASSET_REPO_TOKEN` (PAT) into `./private` | `git clone ${{ secrets.ZRE_REPO_WITH_PAT }}` (the URL with the PAT inside) | `gh api` with `secrets.ASSETS_TOKEN` on the repo `vars.ASSETS_REPO`, file by file |
| Private repo contents | `default.xex`, `default.xexp` (TU), `shader.ar` (`docs/BUILDING.md`) | `files.zip` + `process.sh`/`.ps1` (prepares the ROM), `mm_shader_cache.bin` | `default.xex` and the `TU_*` packages |
| External PRs | `validate-external.yml`: `pull_request_target` + `external` environment (manual approval) | The same as Unleashed | `pull_request` with no secret: the build fails |
| Caches | ccache, vcpkg | ccache | sccache, **and the XEX and TUs** (`actions/cache`, keys `codegen-assets-v1`, `tu-assets-v1`) |
| Platforms | Windows x64, Linux x64, macOS arm64 | Windows x64, Linux x64/arm64, macOS universal | Windows x64, Linux x64/arm64 |
| Linux format | Flatpak (`.flatpak` bundle in a zip) | AppImage + tar.gz + Flatpak | tar.gz |
| Windows format | zip (exe + D3D12/DXC DLLs) | zip (exe + DXC DLLs + SDL2.dll + assets) | zip |
| Signing | None in the workflows | None | None |
| Publishing | CI artifacts on every push; no release workflow in the repo | CI artifacts; no release workflow in the repo | `release.yml` on `v*` tags creates the release (`softprops/action-gh-release`); CI artifacts on every push |
| User installs | Built-in installer: STFS/ISO/folder, XXH3 of every file | Picks the ROM in the menu; only NTSC-U accepted | Wizard (`GameDataSelector`, SDK fork): ISO/STFS/folder, SHA-256 of the XEX |

### 1.2 Unleashed Recompiled

<https://github.com/hedge-dev/UnleashedRecomp>

- Three workflows: `validate.yml` (reusable, `workflow_call`, requires `ASSET_REPO` and
  `ASSET_REPO_TOKEN`), `validate-internal.yml` (push to `main` and PRs from the same repo,
  `secrets: inherit`) and `validate-external.yml` (`pull_request_target` from forks; an `authorize`
  job in the `external` environment gates the build; the approval rule lives in the repository settings, not in the workflow).
- Each job checks out the private repo with `actions/checkout` (`repository:`, `token:`,
  `path: ./private`) and copies it into `UnleashedRecompLib/private/` before configuring. The build
  needs `default.xex`, `default.xexp` and `shader.ar` (`docs/BUILDING.md`).
- Matrix: Linux (Ubuntu 24.04, LLVM 18; Debug/Release/RelWithDebInfo), Windows (clang presets,
  `msvc-dev-cmd`), Flatpak (`flatpak-builder`, runtime `org.freedesktop.Platform` 24.08, with
  `--filesystem=host` so the installer can read the user's files), macOS arm64.
- Windows package: `UnleashedRecomp.exe`, `dxcompiler.dll`, `dxil.dll`, `D3D12/D3D12Core.dll`; the
  PDB as a separate artifact.
- Releases v1.0.2 and v1.0.3 carry `UnleashedRecomp-Windows.zip` and `UnleashedRecomp-Flatpak.zip`.
  The README says builds without Flatpak will come later and points at the Actions artifacts
  meanwhile.
- Steam Deck: Flatpak added as a non-Steam game from Desktop Mode; install in Desktop Mode because
  the file picker may not be available in Game Mode (README, "Steam Deck Support" and "File Picker
  Unavailable on Steam Deck in Game Mode").

### 1.3 Zelda64Recomp

<https://github.com/Zelda64Recomp/Zelda64Recomp>

- The same `validate` / `validate-internal` / `validate-external` structure as Unleashed; the
  secret `ZRE_REPO_WITH_PAT` is a clone URL with the PAT embedded.
- The game-dependent step runs the recompiler in CI (`N64Recomp`, `RSPRecomp`, pinned commit) on
  the ROM taken from the private repo, then builds.
- **AppImage** (`.github/linux/appimage.sh`): built inside the container `dcvz/n64recomp:ubuntu-18.04`
  (old glibc floor). It downloads `linuxdeploy` (continuous) and `linuxdeploy-plugin-gtk`, puts the
  executable and its `assets/` in `AppDir/usr/bin`, runs linuxdeploy, **rewrites `AppRun`** so it
  `cd`s into `usr/bin` before running (where its `assets/` are)
  and supports a portable mode (`portable.txt` next to the AppImage). It **removes the bundled
  `libwayland*`**, `libgmodule*` and the GIO modules, which clash with the host's.
- Release v1.2.2: `Linux-X64.zip`, `Linux-ARM64.zip`, `Linux-Flatpak-X64.zip`, `macOS.zip`,
  `Windows.zip`.
- Steam Deck (README): extract the Linux build, in Desktop Mode right click the executable, "Add to
  Steam".

### 1.4 Nocturne (Castlevania SotN XBLA, ReXGlue)

<https://github.com/birabittoh/NocturneRecomp>; already described in `docs/installer-research.md`
section 1. What matters here:

- `_build.yml` (reusable), `ci.yml` (every push and PR, artifacts named by date and SHA) and
  `release.yml` (tags `v*`: the same build, then a job collects the artifacts and creates the
  release).
- It does not build the SDK: `scripts/download-sdk.py --pinned` downloads its fork's prebuilt SDK.
  `scripts/build.py --package` makes a `.zip` (Windows) or `.tar.gz` (Linux) with the executable and
  the SDK `.so`/`.dll` next to it.
- The XEX goes in `actions/cache` (see 1.7).

### 1.5 Dante's Inferno, AppImage fork (ReXGlue)

<https://github.com/thefixinhixon/hells-gate-recomp> (fork of `florinp93/hells-gate-recomp`). A
small project, but it is the one ReXGlue AppImage found, and it solved the same problems:

- `packaging/appimage/build-appimage-x86_64.sh`: linuxdeploy + appimagetool; `librexruntime.so` in
  `usr/lib` and `librexgpu-xenos.so` both in `usr/lib` and **next to the executable** in `usr/bin`
  (the SDK looks for GPU plugins in the executable's directory). Its own `AppRun` sets
  `LD_LIBRARY_PATH=$HERE/usr/lib`.
- It does **not bundle `libwayland-client`/`-cursor`/`-egl`**: "Bundling them breaks the game's SDL
  Wayland init; the system copies are always compatible."
- At first launch the user picks a writable root folder (`game/`, `config/`, `saves/`, `cache/`,
  `logs/`) and imports their ISO or extracted folder; the AppImage carries no game files.
- Without FUSE: `APPIMAGE_EXTRACT_AND_RUN=1`.
- Built by hand (no workflows); release `v1.0-linux` with `DantesInferno-x86_64.AppImage`.

### 1.6 The ReXGlue SDK's own CI

`~/rexglue-sdk/.github/workflows/_build-platform.yaml`: `linux-amd64` builds inside
`ubuntu:22.04` "so the glibc floor stays at 2.35, which is what every downstream binary inherits.
24.04 puts it at 2.38, above Debian stable and the older SteamOS releases." It installs clang 20
(apt.llvm.org), g++-13 from the `ubuntu-toolchain-r/test` PPA (22.04's libstdc++ 11 has no
`<format>`) and CMake from Kitware. Windows: `choco install llvm cmake ninja`. The SDK is packaged as
a zip of `out/install/<platform>`.

### 1.7 Protecting the secret and the game files

What GitHub guarantees:

- "With the exception of `GITHUB_TOKEN`, secrets are not passed to the runner when a workflow is
  triggered from a forked repository" (GitHub Docs, *Using secrets in GitHub Actions*).
- "If the environment requires approval, a job cannot access environment secrets until one of the
  required reviewers approves it." Environments can also be limited to selected branches and tags.
  On GitHub Free, environment secrets exist only in public repositories, which is our case
  (*Deployments and environments*).
- **Caches**: "If a workflow run is triggered for a pull request, it can also restore caches created
  in the base branch, including base branches of forked repositories", and "Anyone with read access
  can create a pull request on a repository and access the contents of a cache"
  (*Dependency caching reference*). So Nocturne's cached XEX can be read by anyone through a PR,
  and the same holds for ccache/sccache contents of a build that compiled `generated/`.
- `actions/checkout` accepts `token` (a PAT) or `ssh-key`, and removes either in its post-job step;
  `persist-credentials: false` keeps it out of the working copy's git config.

What the references do and what is worth avoiding:

- Unleashed and Zelda64Recomp run `pull_request_target` and check out the PR's code with the secret
  present, gated by an environment approval. GitHub Security Lab calls this pattern a "pwn request":
  the approval is the only barrier between external code and the secret.
- All three use a PAT. A read-only deploy key on the private repository grants access to that
  repository only, which limits the damage of a leak.

## 2. Formats

### 2.1 Linux: AppImage

**How an AppImage works.** A runtime (ELF) with a SquashFS image appended; at startup it mounts the
image through FUSE and runs `AppRun`. The current runtime (`AppImage/type2-runtime`, used by
`appimagetool`) is static: "Since the runtime is linked statically, libfuse2 is no longer required
on the target system" (it uses `fusermount3`). With no FUSE at all, the image can be run with
`APPIMAGE_EXTRACT_AND_RUN=1` or `--appimage-extract`. Inside, everything is read-only.

**What does not get bundled.** AppImage's exclusion list (`pkg2appimage/excludelist`, used by
linuxdeploy) leaves on the host glibc (`libc`, `libm`, `ld-linux`), `libstdc++.so.6`, `libgcc_s`,
the GL/EGL/GLX drivers, `libdrm`, `libgbm`, `libX11`, `libxcb`, `libX11-xcb`, `libwayland-client`,
`libasound`, `libpipewire`, `libz`, among others. The consequence: **the glibc and libstdc++
versions the binary needs have to exist on the host**, so the build is done on an old base.

**Torchlight today** (RelWithDebInfo build, read with `readelf -d`/`objdump -T`):

| Item | Today | In an AppImage |
|---|---|---|
| `torchlight` NEEDED | `librexruntime`, `libTracyClient` (rd builds), `libOgreMain`, `libOgreRTShaderSystem`, `libX11`, `libz`, `libstdc++` | Release config: `librexruntime.so` has no Tracy dependency |
| `torchlight` RUNPATH | `~/ogre14-install/lib:~/ogre14-install/lib/OGRE:$ORIGIN` (`src/backend/CMakeLists.txt` adds absolute rpaths; the SDK adds `$ORIGIN`) | `$ORIGIN/../lib` only |
| SDK libraries | `librexruntime*.so` is not copied next to the executable on Linux (the SDK's `rexglue_configure_target` only does that on Windows and macOS), hence the `LD_LIBRARY_PATH` in the README. Their own RUNPATH is `$ORIGIN:$ORIGIN/../lib` | Copied to `usr/lib`; already relocatable |
| GPU plugins | `librexgpu-{null,xenos}.so` next to the executable (our CMake stages them) | The same, in `usr/bin` |
| OGRE libraries | RUNPATH `~/ogre14-install/lib`; the Wayland GL3+ plugin's RUNPATH is the build directory (`~/ogre-14.6.0/build-wayland/lib`) | Built with `CMAKE_INSTALL_RPATH` relative to `$ORIGIN` |
| OGRE plugin directory | `TORCHLIGHT_OGRE_PLUGIN_DIR`, an absolute path compiled in (`live_mode.cpp:207-210`) | Relative to `platform::ExecutableDir()` (which reads `/proc/self/exe` and so resolves to the mount point) |
| OGRE media (`Main`, `RTShaderLib`, 360 KB) | `TORCHLIGHT_OGRE_MEDIA_DIR`, absolute, compiled in (`backend.cpp:1417-1419`) | Bundled, relative to the executable |
| `data/ui` | Next to the executable, found through `ExecutableDir()` | Already works (read-only is fine) |
| glibc | `GLIBC_2.43` required (built on the development machine, glibc 2.43) | 2.35 floor if built on `ubuntu:22.04`, like the SDK CI |
| libstdc++ | `GLIBCXX_3.4.31` (GCC 13) | The same with g++-13 on 22.04; bundled with a check at startup (section 5.7, decision 2) |

**What the game writes** (the AppImage forbids writing next to the executable):

| What | Where today | OK in an AppImage? |
|---|---|---|
| Host settings (`settings.toml`) | `platform::ConfigDir()`, XDG config | Yes |
| RTSS shader cache | `platform::ShaderCacheDir()`, XDG cache | Yes |
| Saves, SDK cache | `user_data_root`, default `GetUserFolder()/torchlight` (`rex_app.cpp:115-135`) | Yes |
| `import/` (saves research) | `<game_data_root>/import/`, the user's game folder | Yes if the game data is in a writable place (section 5.5) |
| SDK config `torchlight.toml` | **`exe_dir/torchlight.toml`** (`rex_app.cpp:146`); only read, but it is where `game_data_root` would persist | No: needs `OnConfigurePaths` (`torchlight_app.h:56` has it commented out) to move it to the config folder |
| SDK logs | **`exe_dir/logs`** unless `log_file` is set (`rex_app.cpp:161-171`) | No: must move to a user folder |
| F9 captures, `--live_record` | Only with explicit flags | Yes (the user passes the path) |

**Steam Deck as a non-Steam game.** SteamOS 3.7 and 3.8 ship glibc 2.41 (DistroWatch), above the
2.35 floor. A non-Steam shortcut runs the binary on the host, by default outside the Steam Linux Runtime
(not verified on the device). In Game Mode, gamescope gives the game an XWayland display, so SDL
picks `x11` and the backend uses its X11 child window; this is reasoned, not verified on the
device. Steam Input presents the controls as an Xbox pad. As in Unleashed, a file picker may fail
in Game Mode: the first setup is better done in Desktop Mode.

**Building the AppImage.** The two references use linuxdeploy (finds and copies dependencies,
patches RUNPATH, skips the exclusion list) and appimagetool (static runtime). The proposal is the
same, with the precautions both learned: do not bundle `libwayland-*` and give the GPU plugins their
place next to the executable.

### 2.2 Windows: zip (after the beta)

The three references publish a zip with the executable and its DLLs next to it; none uses an
installer or signs. For Torchlight: `torchlight.exe`, `rexruntime.dll`, `rexgpu-null.dll`,
`rexgpu-xenos.dll`, OGRE (`OgreMain.dll`, `OgreRTShaderSystem.dll`, `RenderSystem_GL3Plus.dll`,
`Codec_STBI.dll`), OGRE media, `data/ui`, licenses. Settings and the shader cache go to `%APPDATA%`
and `%LOCALAPPDATA%` (`docs/windows-port.md`); the logs next to the executable are a problem if the
user extracts into a protected folder (the same fix as in 2.1). The C runtime: clang with the MSVC
ABI uses the MSVC runtime, so either the zip carries `vcruntime140.dll`/`msvcp140.dll` (Microsoft
allows app-local redistribution) or everything is built with the static runtime. Which one the
references use was not checked. An unsigned executable gets the SmartScreen warning; none of the
references avoids it.

## 3. What CI builds besides the game

Neither one contains game content, so both can be cached and even published:

1. **ReXGlue SDK with our patches.** Base `bd833a2` (`patches/README.md`), the patches of `patches/series` in their
   order with `git apply`, install with the platform preset (`linux-amd64`, `win-amd64`). Inside
   `ubuntu:22.04` with the SDK CI's toolchain (clang 20, g++-13, Kitware CMake, its `apt` package
   list). On Windows the POSIX patch (`rexglue-posix-wait-fraction.patch`) does not apply
   (`docs/windows-port.md`, WIN.2). Cache key: SDK commit + hash of `patches/` + toolchain image.
   Only the Release configuration is needed for the release (the SDK also builds Debug and
   RelWithDebInfo; the build time can be cut by building one).
2. **OGRE 14.6.0** with the recipe in docs/BUILDING.md (GL3+, RTSS, STBI) and the second GL3+ build with
   `OGRE_USE_WAYLAND` for `lib/OGRE/wayland/`, plus `-DCMAKE_INSTALL_RPATH='$ORIGIN;$ORIGIN/..'`
   (or patchelf afterwards) so the install is relocatable. On Windows, without the Wayland build.
   Cache key: OGRE tag + hash of the recipe.

The OGRE recipe is today only in docs/BUILDING.md; the cross-platform stage of `ARCHITECTURE.md` already
asks for it to be described inside the project. A script used by CI and by developers (one source)
would serve both.

## 4. Legal considerations

This is a description of what the projects do and how they say it, not legal advice.

**What they publish and what not.** All of them publish an executable that contains the game's
code, translated by the recompiler and compiled. None publishes the original executable (XEX/ROM),
the assets, or the generated source. All of them require the user's own copy and check that it is
exactly the expected version (hash); none checks that the user bought it (`installer-research.md`).
The private repository with the XEX is still a copy of copyrighted material stored on GitHub; it is
only not public.

**How they say it:**

- Unleashed: "This project does not include any game assets. You must provide the files from your
  own legally acquired copy of the game to install or build Unleashed Recompiled." It also warns to
  download only from its Releases page ("We will never distribute builds on other websites").
- Zelda64Recomp: "This repository and its releases do not contain game assets. The original game is
  required to build or run this project."
- Nocturne: "You must own the game. This project does not ship any copyrighted code, data, or
  assets." The "code" part is inexact: the executable is compiled from the game's code.

**The gray zone** is the executable: it is derived from the copyrighted code. The rights holder can
ask GitHub to remove a release or the repository (DMCA) at any time, whatever the README says. Not
researched: whether any recomp has received a takedown.

**Torchlight specifics:**

- **Trial**: the XBLA package is the same for the demo and the full game (`installer-research.md`).
  With `license_mask = 1` in a published build, whoever has the free demo package plays the full
  game. **Decided (2026-10-05): the published binaries keep `license_mask = 1`; the project owner
  takes that risk, and if a DMCA notice arrives the repository is taken down.**
- Wording that does not overstate: "The releases contain no game assets and no original game files.
  The executable is built from the game's code and only runs with files from your own copy of
  Torchlight (Xbox LIVE Arcade)." Plus "unofficial, not affiliated with" the rights holders.
- Our own icon and name for the AppImage/`.desktop`, not the game's `TitleIcon.png` or logo.
- Nothing from the game or from third party translations in the repo or in the packages (already a
  project rule).
- The repo's license: GPL-3.0 for the project (`LICENSE`) and BSD-3-Clause for the SDK patches
  (`patches/LICENSE`), chosen by the owner in `f37b2ad` (2026-10-05). Every package also needs the
  third-party notices (`THIRD_PARTY_NOTICES.md`): OGRE (MIT), ReXGlue
  SDK (BSD-3-Clause, including the null GPU plugin patch from Rayman Origins Recompiled,
  `patches/README.md` item 8), SDL3 (zlib) and the rest of what the SDK links statically.

## 5. Proposal for Torchlight

### 5.1 Content boundary

- **Public repository**: our code, `config/` and the manifest (facts about the XEX, as today), the
  SHA-256 of `default.xex` (`87e4e1cd…`) and of the data files the game needs, workflows and
  packaging scripts.
- **Private repository** (`torchlight-assets`, owned by the project owner): only
  `default.xex`. The build needs nothing else from the game (codegen reads the XEX named in the
  manifest; `pak.zip` and the rest are read at runtime from the user's copy). To confirm in the
  first release run.
- **Published** (Releases only): the AppImage and the Windows zip, a `SHA256SUMS` file
  and the build attestation. Separately, as their own public downloads, the patched SDK and OGRE
  builds (no game content; decision 5). Never: the XEX, `generated/`, debug symbols (private,
  decision 4), intermediate artifacts, captures or sessions.
- **Private, never in the public repository's caches or artifacts**: the XEX and the debug symbols
  of each release.

### 5.2 Private repository and secret

- Read-only **deploy key** on `torchlight-assets`; its private half as the secret
  `ASSETS_DEPLOY_KEY` of a `release` **environment** in the public repo, with the owner as required
  reviewer and deployments allowed only from `v*` tags. The repository name goes in a variable.
- `actions/checkout` with `ssh-key` and `persist-credentials: false`, into a path outside the build
  directory; the next step checks the XEX's SHA-256 against the one in the public repo and fails if
  it differs.

### 5.3 Public CI (`ci.yml`): no game

On push and `pull_request` (never `pull_request_target`), no secrets, `permissions: contents: read`.

1. **deps** job (Linux in `ubuntu:22.04`): build or restore the patched SDK
   and OGRE (section 3). Every Linux build job (deps, test, game-linux) runs inside the container
   `ubuntu:22.04` on a runner pinned to `ubuntu-24.04` (not `ubuntu-latest`, which moves to Ubuntu
   26 on 2026-10-19): what the binaries depend on is the container's (glibc 2.35, the toolchain of
   `tools/deps/ubuntu_toolchain.sh`), not the runner's. The Windows jobs run on `windows-2022`
   directly. `tools/deps/key.sh` hashes that system's name (container or runner image) with the
   scripts and patches, and the cache keys also spell it (`deps-linux-amd64-ubuntu22.04-<key>`,
   `deps-windows-amd64-windows2022-<key>`), so dependencies built on different systems never mix.
   The package checks run in `ubuntu:22.04` and `archlinux:latest` on purpose: Arch is the rolling
   distribution the AppImage must keep working on. Caching is safe here. On `main`, when the cache key changes, the job also
   publishes both builds as public downloads (decision 5), so developers and the release workflow
   download them instead of building. **deps-windows** does the same on `windows-2022`:
   `tools/build-deps/windows.ps1` builds zlib, OGRE (RelWithDebInfo, what the game's Release
   links) and, with `-SdkPrefix`, the SDK (Release; the same repository and commit as
   `build_sdk.sh`, the series without the `posix` patches), with LLVM 21.1.8 from
   `tools/build-deps/windows_toolchain.ps1` and the runner's MSVC; key `tools/deps/key.sh windows`,
   published as `deps-windows-<key>` (`sdk-windows-amd64.zip`, `ogre-windows-amd64.zip`).
   **test-windows** builds and tests with the preset `win-amd64-nogame`. Same triggers as Linux.
2. **test** job: configure with the game-free option (5.6, REL.1), build and `ctest`. Today that
   covers `guest_abi_layout_test`, `commands_test`, `rtss_test`, `resource_registry_test`,
   `session_test`, `host_settings_test`, `video_menu_model_test`, `menu_strings_test`,
   `text_conversion_test`, `gpu_choice_test`, `conventions_test`, the `live/` tests
   (`measured_mutex`, `snapshot_store`, `session_file`, `frame_queue`, `slow_frames`,
   `live_content_source`, `ui_overlay`, `ui_gamepad`), `detile_test` and the Python tests
   (`save_convert_test`, `tl_translate_test`). `ui_pass_test` and `render_scale_test` need a display
   and skip with code 77; with Mesa llvmpipe and Xvfb (or surfaceless EGL) they could run. On
   Windows they also run with Direct3D 11 (`<name>_d3d11`), which the runner draws in software
   (WARP); their GL3+ runs (label `opengl33`) are left out there, as the runner has no OpenGL 3.3.
3. **package-check** job: package the `replay` tool (it links the backend and loads the OGRE plugins
   and media like the game) with the same AppImage layout and run it in clean `ubuntu:22.04` and
   `archlinux` containers on a synthetic capture. It validates relocatable paths and bundled
   libraries on every PR without the game.

### 5.4 Release workflow (`release.yml`)

Trigger: `v*` tags and `workflow_dispatch`. All game-dependent jobs run in the `release`
environment (approval first), on GitHub-hosted runners only.

1. **Dependencies**: each game job downloads the SDK and OGRE CI published for the commit's key
   (`deps-<key>`, `deps-windows-<key>`) and fails if there are none: CI on `main` publishes them.
2. **game-linux** (container `ubuntu:22.04`): checkout, deps from the previous job, private
   checkout + hash check, `rexglue codegen`, Release build with debug info, `ctest`,
   `cmake --install` into an AppDir, debug info split from every ELF we build (`objcopy
   --only-keep-debug`, then strip, keeping the build ID), linuxdeploy (without `libwayland-*`) +
   appimagetool (static runtime), the bundled `libstdc++` and its startup check (decision 2),
   `ldd` check of every ELF in the extracted image in clean `ubuntu:22.04` and `archlinux`
   containers, upload of the AppImage only.
3. **symbols**: the symbols go to a private repository (`torchlight-symbols`, pushed with a
   second deploy key with write access, in the same environment), one directory per tag and
   platform: `<tag>/linux-x86_64/` (the `.debug` files, matched to crashes by build ID) and
   `<tag>/windows-x86_64/` (the PDBs, `packaging/windows/collect_symbols.ps1`: `torchlight.pdb` and
   OGRE's, at the binary's path in the zip). Both jobs push to it; on a race the second rebases
   and pushes again. Never `upload-artifact` in the public repository: its artifacts are readable
   by anyone.
4. **game-windows** (`windows-2022`): the same as game-linux up to the zip (codegen with the SDK's
   `rexglue.exe`, Release with `-g`, `ctest`, `cmake --install`, the PDBs collected and removed,
   `make_zip.ps1`, `check_zip.ps1`), upload of the zip only. **check-windows** runs `check_zip.ps1`
   again on a second runner, on the downloaded zip. Runners have no Windows Sandbox and no GPU: the
   clean-machine run is `packaging/windows/sandbox_check.ps1`, by hand.
5. **publish**: draft release with the files, `SHA256SUMS` and
   `actions/attest-build-provenance`. The owner publishes it after trying it on real hardware
   (Linux desktop and Steam Deck): running the game is an agreed milestone, not a CI step.

Rules for every job that touches the game (decision 3): no `actions/cache` and no compiler cache,
neither ccache nor sccache (they would store the XEX or compiled game code, readable through any
PR); no `upload-artifact` of `generated/`, the build directory or the symbols; no game data printed
in logs. Builds of every push, as Nocturne publishes, are left out: each binary with
the game's code is published on purpose, from a tag.

### 5.5 What the user does

1. Download the AppImage (the zip on Windows, after the beta), mark it executable.
2. First launch without game data: the game asks for the Torchlight XBLA package (the `LIVE` file)
   or an already extracted folder, checks the title ID and the SHA-256 of `default.xex` and the data
   files, and extracts/copies into a writable data folder (`~/.local/share/torchlight/game/` on
   Linux; `%LOCALAPPDATA%` on Windows, after the beta), where `import/` can also live.
   `game_data_root` is stored in the config so it does not ask again. The SDK already reads STFS
   packages (`StfsContainerDevice`), so extraction needs no external tool. `--game_data_root` keeps
   working for developers.
3. Steam Deck: do step 2 in Desktop Mode, then "Add to Steam" and play from Game Mode.

### 5.6 Tasks, by area

Areas and owners: **render & platform** is the agent that owns `src/backend`, `src/frontend`,
`src/live`, `src/platform` and CMake; **release** is the build, packaging and CI work (new files in
`tools/`, `packaging/` and `.github/`), still without an assigned agent; **owner** is the project
owner. Each task is split into what the Linux beta needs and what comes after the beta (Windows).

| # | Change | Area / owner | Linux beta | After the beta (Windows) | Done when |
|---|---|---|---|---|---|
| REL.1 | Configure without `generated/` (game-free option; today `include(generated/rexglue.cmake)` is unconditional) | CMake / render & platform | Yes | The same option on `win-amd64` (with WIN.1) | `ci.yml`'s test job builds and passes on a clean runner |
| REL.2 | Relocatable layout: `install()` rules for the game, SDK libraries, GPU plugins, OGRE libraries/plugins/media and `data/ui`; plugin and media directories relative to `ExecutableDir()`; RUNPATH `$ORIGIN/../lib`; no absolute paths | CMake, backend, live / render & platform | Yes | DLLs next to the executable, no RUNPATH | The installed tree runs from any path without `LD_LIBRARY_PATH` (replay check in 5.3) |
| REL.3 | SDK config and logs out of the executable's directory (`OnConfigurePaths`, `log_file`) | App, platform / render & platform | Yes | `%APPDATA%`/`%LOCALAPPDATA%` paths (part of WIN.4) | Running from a read-only directory leaves nothing next to the executable |
| REL.4 | First-run game data setup (5.5) with hash checks, extraction with the SDK's STFS reader | App, platform / render & platform | Yes | Windows folder and dialogs | A wrong XEX is rejected with a clear message; the right package ends in a playable install |
| REL.5 | Deps scripts (patched SDK, OGRE recipe) used by CI and developers, and their public prebuilt downloads | Build / release | Linux | `win-amd64` SDK (without the POSIX patch) and OGRE | CI and the README use the same script; the downloads exist for the current key |
| REL.6 | Packaging: AppDir/AppImage script with bundled `libstdc++` and its startup check, debug info split | Packaging / release | AppImage | Windows zip with PDBs split | 5.3's package-check passes on `ubuntu:22.04` and `archlinux` |
| REL.7 | `ci.yml` and `release.yml` (environment, deploy keys, symbols repository, no caches in game jobs) | CI / release | Linux jobs | Windows jobs, checksums and attestation for the zip (decision 6) | A dry-run tag produces a draft release with the AppImage, `SHA256SUMS` and the attestation |
| REL.8 | `LICENSE`, third-party notices in the packages, README text (section 4, including the `license_mask` and DMCA position) | Docs / owner + release | Yes (blocks the first release) | The same notices in the zip | The owner chose the license; every package carries the notices |

Order for the beta: REL.1 and REL.5 first (they unblock `ci.yml`), then REL.2 and REL.3 (the
package-check), REL.4, REL.6 and REL.7; REL.8 before the first tag.

Owner tasks outside the code: create `torchlight-assets` (the XEX, plain) and `torchlight-symbols`
(private), their deploy keys, and the `release` environment (owner as required reviewer, `v*` tags
only).

### 5.7 Decisions (2026-10-05)

1. **`license_mask = 1` in the published binaries.** The owner takes the risk that any copy of the
   free demo package plays the full game (section 4). If a DMCA notice arrives, the repository is
   taken down.
2. **Minimum distribution: bundled libstdc++ with a check at startup.** The AppImage carries
   `libstdc++.so.6` (and `libgcc_s.so.1`) from the build image outside the default library path;
   `AppRun` compares the highest `GLIBCXX_` version of the host's library with the bundled one and
   uses the bundled one only when it is newer. The selection lives only inside `AppRun` (the
   launcher's own mechanism, not a setting for the user). The glibc floor stays at 2.35
   (`ubuntu:22.04`).
3. **The XEX in the private repository as a plain file**, as the references do. Never in caches,
   compiler caches included (ccache, sccache), in any job that touches the game.
4. **Debug symbols are private**, only for analyzing crashes: split in the release job and pushed to
   a private repository, never as public artifacts.
5. **The patched SDK and OGRE are published as prebuilt downloads** (no game content).
6. **Windows**: no signing, with `SHA256SUMS` and the build attestation (confirmed for the beta on
   2026-10-06; the README and the release notes explain SmartScreen's prompt and how to pass it).

**Scope**: first a Linux-only beta (AppImage). Windows is added when the port is ready; the owner
announces it.

**Was blocking the first release**: the repository's `LICENSE` (REL.8); the owner chose GPL-3.0 in
`f37b2ad`.

### 5.8 Pending after the beta

- **The AppImage runtime in a release of our own.** `packaging/linux/get_tools.sh` pins
  type2-runtime `8f39b89` by SHA-256, but upstream only publishes it as its `continuous` release,
  which each new upstream build replaces: from then on the download no longer matches and the
  build stops until the pin and `THIRD_PARTY_NOTICES.md` are updated. To be independent of that,
  the owner uploads the same verified file (`runtime-x86_64`, SHA-256 `156f4bdb…`, signed by
  type2-runtime's key) as an asset of a release of this repository, and `get_tools.sh` downloads
  it from there with the same hash.
- **Re-running a release whose symbols were already pushed.** Each game job's Symbols step
  commits `<tag>/<platform>/` to `torchlight-symbols` and fails when there is nothing new to commit
  (a re-run with the same binaries: `git commit` exits 1). For v0.1.0-beta the owner deleted the
  folders and re-ran. To fix: skip the commit and the push when the tree did not change, and say
  so in the log.
- **Guest memory reads on Windows and macOS arm64, before the macOS port.** The generated code
  translates a guest address as `base + address`, plus 0x1000 from 0xE0000000 up on Windows and
  macOS arm64 (`REX_PHYS_HOST_OFFSET`, `rex::memory::GuestPtr`). The content snapshots of buffers
  and textures translate that way since 2026-10-07 (`capture/guest_readers.cpp`, `GuestBytes`), but
  the scalar readers of `guest_abi` (`ReadU32`, `ReadU64` and the rest, `ogre_layout.h`) add
  nothing. On Linux that is right; on those platforms a read from 0xE0000000 up would be 4 KB
  off. They read the game's OGRE and Xbox D3D objects, expected in the guest's virtual heaps, below
  0xE0000000; that is not checked for every reader yet.
  To resolve, without platform `#ifdef`s in `guest_abi`, one of:
  1. the readers take a guest memory view instead of a bare `membase` (the base plus the
     translation, built once by the caller with `rex::memory::GuestPtr`), so `guest_abi` stays
     free of the SDK and is still testable with a fake memory (preferred);
  2. `guest_abi` includes the SDK's `rex/system/xmemory.h` and uses `GuestPtr` (simple, but
     `guest_abi` stops building on its own);
  3. the readers keep `base + address` and state the invariant (no reads from 0xE0000000 up),
     checked in a test and in the diagnostics build.

### 5.9 Branches and releases (since 2026-10-07)

- **`develop`** is where the work is integrated. Work happens on branches made from `develop` and
  merged back into it (the project's agents integrate directly; external contributors open pull
  requests against `develop`, `CONTRIBUTING.md`).
- **`main`** only receives releases. At a feature freeze `develop` is merged into `main`, the
  release is tagged on `main` (`vX.Y.Z`, annotated) and the tag starts `release.yml`. Nobody pushes
  to `main` otherwise.
- **Hotfix**: a branch from `main` with the fix, merged into `main` and tagged there, then merged
  back into `develop` so the fix is not lost at the next freeze.
- **CI** (`ci.yml`) runs on pushes to `develop` and `main`, on pull requests into either, and by
  hand. The SDK and OGRE builds (`deps-<key>`, `deps-windows-<key>`) are published from pushes to
  either branch, at the commit that built them (`--target`), and every run first looks for a
  published one with its key. After a freeze merge both branches point at the same commit and two
  runs may build the same key at once: the second `gh release create` fails, and the step then
  counts the release it finds as published.
- **Release** (`release.yml`): only from `v*` tags whose commit is on `main`. The `release`
  environment allows deployments from `v*` tags but cannot tell which branch a tag is on, so the
  first job, *Tag on main*, checks that the tagged commit is an ancestor of `origin/main` and stops
  the run before the approval is asked for otherwise: a `v*` tag on `develop` cannot publish. The
  check is in the `release.yml` of the tagged commit, so it covers every commit of `develop` from
  the one that added it on.
- **OGRE in the Windows release is RelWithDebInfo** (MSVC `/O2 /Ob1`: inlining limited to functions
  marked inline), while the game and the SDK are Release and the Linux release's OGRE is `-O3`.
  `ci.yml`'s deps-windows builds it with `tools/build-deps/windows.ps1 -Configs RelWithDebInfo`, and
  the top `CMakeLists.txt` maps the game's Release to those libraries
  (`CMAKE_MAP_IMPORTED_CONFIG_RELEASE Release RelWithDebInfo`). Measured 2026-10-07 against OGRE
  Release (`/O2 /Ob2`) in the native mode on Direct3D 11 (docs/performance-profile.md, "OGRE Release
  against RelWithDebInfo on Windows"): no measurable difference, the game's main thread being the
  limit. The release keeps RelWithDebInfo, and with it OGRE's symbols. The dependency key
  covers the configurations since 2026-10-07: `tools/deps/key.sh` holds the ones CI builds for
  Windows, hashes them into `key.sh windows`, and prints them with `key.sh windows-configs`, which
  `ci.yml`'s deps-windows passes to `windows.ps1 -Configs` and `-SdkConfigs`. Changing them there
  changes the Windows key (and not the Linux one), so CI builds and publishes a new
  `deps-windows-<key>` instead of reusing the RelWithDebInfo one.

## Sources

- Unleashed Recompiled: <https://github.com/hedge-dev/UnleashedRecomp> (`.github/workflows/`,
  `flatpak/`, `docs/BUILDING.md`, `README.md`), releases
  <https://github.com/hedge-dev/UnleashedRecomp/releases>.
- Zelda64Recomp: <https://github.com/Zelda64Recomp/Zelda64Recomp> (`.github/workflows/`,
  `.github/linux/appimage.sh`, `README.md`), releases
  <https://github.com/Zelda64Recomp/Zelda64Recomp/releases>.
- NocturneRecomp: <https://github.com/birabittoh/NocturneRecomp> (`.github/workflows/_build.yml`,
  `ci.yml`, `release.yml`, `scripts/build.py`, `README.md`).
- Dante's Inferno AppImage fork: <https://github.com/thefixinhixon/hells-gate-recomp>
  (`packaging/appimage/`).
- ReXGlue SDK: local checkout `~/rexglue-sdk` at `bd833a2` (`.github/workflows/_build-platform.yaml`,
  `cmake/rexglue_helpers.cmake`, `src/ui/rex_app.cpp`); <https://github.com/rexglue/rexglue-sdk>.
- AppImage runtime: <https://github.com/AppImage/type2-runtime>; appimagetool:
  <https://github.com/AppImage/appimagetool>; exclusion list:
  <https://github.com/AppImageCommunity/pkg2appimage/blob/master/excludelist>; linuxdeploy:
  <https://github.com/linuxdeploy/linuxdeploy>.
- SteamOS package versions: <https://distrowatch.com/steamos>.
- GitHub Docs: secrets
  <https://docs.github.com/en/actions/how-tos/write-workflows/choose-what-workflows-do/use-secrets>;
  environments
  <https://docs.github.com/en/actions/reference/workflows-and-actions/deployments-and-environments>;
  caching
  <https://docs.github.com/en/actions/reference/workflows-and-actions/dependency-caching>;
  `actions/checkout` <https://github.com/actions/checkout>.
- GitHub Security Lab, "Preventing pwn requests":
  <https://securitylab.github.com/resources/github-actions-preventing-pwn-requests/>.
- This repository: `README.md`, `docs/ARCHITECTURE.md`, `docs/installer-research.md`,
  `docs/windows-port.md`, `patches/README.md`, and `readelf`/`objdump` of the current build and of
  `~/ogre14-install`.
