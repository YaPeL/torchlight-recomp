# torchlight-recomp

## Build and run

Requirements: the ReXGlue SDK with the patches from `patches/` (in the order of `patches/series`),
OGRE 14.6.0, and the game's files (the first start installs them from your package, see below; for
development they can also be passed with `--game_data_root`). All commands are run from
`~/torchlight-rewrite`.

The SDK and OGRE are built with the same scripts CI uses (`tools/deps/`; Release; CI publishes them
as `deps-<key>` downloads, `tools/deps/key.sh`). On Ubuntu 22.04, `tools/deps/ubuntu_toolchain.sh`
(as root) installs the toolchain CI uses: clang 21, libstdc++ 13, a recent CMake:

```sh
tools/deps/build_sdk.sh ~/rexglue-sdk/out/install/linux-amd64 "" all
tools/deps/build_ogre.sh ~/ogre14-install
```

OGRE: GL3+ (EGL), the RTSS with its shaders and the STBI codec; the GL3+ plugin is built a second
time for Wayland windows, in `lib/OGRE/wayland/` (OGRE builds its window support for X11 or for
Wayland). `build_ogre.sh PREFIX WORK RelWithDebInfo` gives OGRE with debug information; another
OGRE path: cache variable `TORCHLIGHT_OGRE_INSTALL`. `all` builds the SDK's Debug, Release and
RelWithDebInfo libraries, which the debug and relwithdebinfo presets link; without it, Release only
(what CI and the published game use).

A build directory configured earlier with the OGRE 1.6.1 backend needs `cmake --fresh --preset ...`
(the cache keeps the old OGRE path).

On Windows, `tools/build-deps/windows.ps1` builds the host dependencies into
`%USERPROFILE%\ogre14-install` (the default of `TORCHLIGHT_OGRE_INSTALL` there): zlib 1.3.2 (static;
Windows has no system zlib) and OGRE 14.6.0 with the options above (no Wayland build), in Debug and
RelWithDebInfo, with the dynamic C runtime the SDK uses (`/MD`, `/MDd` in Debug, checked after the
build). zlib is built with clang like the game; OGRE with MSVC's `cl.exe`, because OGRE's Win32 GL
code has two constructs clang rejects (the script lists them). It needs Visual Studio 2022 or its
Build Tools (MSVC x64 tools, a Windows SDK, the CMake tools), LLVM 21 and Git:

```powershell
powershell -ExecutionPolicy Bypass -File tools\build-deps\windows.ps1 [-Prefix DIR] [-WorkDir DIR] [-SdkPrefix DIR]
```

With `-SdkPrefix` it also builds the ReXGlue SDK with `patches/series` (the Windows counterpart of
`tools/deps/build_sdk.sh`; every configuration, or `-SdkConfigs`) and installs it there: that
folder is the `-DCMAKE_PREFIX_PATH` below. CI builds the same with `-SdkConfigs Release
-Configs RelWithDebInfo`, with the LLVM of `tools/build-deps/windows_toolchain.ps1`.

Regenerate the recompiled code (only needed if `generated/` does not exist or
`config/torchlight_functions.toml` changed; the build also reruns it if its inputs change):

```sh
~/rexglue-sdk/out/install/linux-amd64/bin/rexglue codegen torchlight_manifest.toml
```

Build (configure + build, Debug):

```sh
cmake --preset linux-amd64-debug
cmake --build --preset linux-amd64-debug -j"$(nproc)"
```

Optimized build with symbols (RelWithDebInfo; the one used to measure performance):

```sh
cmake --preset linux-amd64-relwithdebinfo
cmake --build --preset linux-amd64-relwithdebinfo -j"$(nproc)"
```

The executable ends up in `out/build/linux-amd64-relwithdebinfo/torchlight` (the commands below use
that one; with Debug, `out/build/linux-amd64-debug/torchlight`).
Its RUNPATH has the SDK's and OGRE's library directories, so it needs no `LD_LIBRARY_PATH`; OGRE's
plugins and media are linked next to it in `ogre/`.

A relocatable install (the executable, the GPU plugins, `data/ui` and `ogre/` in `bin/`, the shared
libraries in `lib/`, RUNPATH `$ORIGIN/../lib`) runs from any folder, read-only included, without
`LD_LIBRARY_PATH`; `--component tools` adds the `replay` tool:

```sh
cmake --install out/build/linux-amd64-relwithdebinfo --prefix DIR
cmake --install out/build/linux-amd64-relwithdebinfo --prefix DIR --component tools
```

On Windows (from a shell with the MSVC x64 environment, `vcvars64.bat`, and LLVM 21 first in
`PATH`; the SDK installed with the `win-amd64` preset, see `patches/README.md`):

```bat
cmake --preset win-amd64-relwithdebinfo -DCMAKE_PREFIX_PATH=<sdk>/out/install/win-amd64
cmake --build --preset win-amd64-relwithdebinfo
ctest --test-dir out/build/win-amd64-relwithdebinfo --output-on-failure
```

There every executable (the game, the replay, the tests) ends up in the build directory itself,
next to the OGRE and SDK DLLs it needs (Windows has no rpath). Settings go to
`%APPDATA%\TorchlightRecomp\`; the shader cache, the logs and the installed game files to
`%LOCALAPPDATA%\TorchlightRecomp\` (`ogre\`, `logs\`, `game\`); saves to
`%USERPROFILE%\Saved Games\TorchlightRecomp\`.

The Linux AppImage (what the release workflow does, `docs/release-pipeline.md`): a Release build
installed to a folder, its symbols split, then
`packaging/linux/get_tools.sh TOOLS && PATH=TOOLS:$PATH packaging/linux/make_appimage.sh INSTALL
OUT.AppImage`; `packaging/linux/check_appimage.sh OUT.AppImage` checks it.

Without the game (libraries, tests and tools only, no `generated/`; what CI builds):
`cmake --preset linux-amd64-nogame` (with `-DCMAKE_PREFIX_PATH=<SDK install>` when the SDK is not
found by itself).

Without `--game_data_root` the game uses its own copy of the game files in
`~/.local/share/TorchlightRecomp/game/`: on the first start it asks for your Torchlight XBLA package
(or an extracted folder), checks it against the supported version (1.0.140.0) and installs it
there. `game_setup_test PACKAGE` checks a package the same way without starting the game. The game
runs as the full game (`license_mask` 1); `--license_mask=0` gives the demo.

The first configure downloads miniz 3.0.2 (for the save import, `src/save_import/CMakeLists.txt`,
pinned by the SHA-256 of its release zip). Without network, extract that zip somewhere and point
CMake at it: `-DFETCHCONTENT_SOURCE_DIR_MINIZ=/path/to/miniz-3.0.2` (the folder with `miniz.c`).

Run (by default `--native_live=only`: the native OGRE backend is the only renderer, Xenos off). The
app forces the `null` GPU plugin and the backend draws inside the game's window (fullscreen,
letterboxed if the aspect ratio is not 16:9), which keeps focus and input (keyboard, gamepad, F9),
with the video driver SDL picked (wayland or x11; to force X11, `--video_driver=x11`). The GPU, the
internal resolution, the FPS cap, vsync and the language are chosen in the game's menu (Options →
Settings, *Video* column) and saved in `~/.config/TorchlightRecomp/settings.toml`:

```sh
./out/build/linux-amd64-relwithdebinfo/torchlight \
  --game_data_root $HOME/360tools/extracted/extracted --capture_dir $PWD/out/captures
```

F9 captures (`--capture_frames` frames) to `--capture_dir`.

ReXGlue's emulated GPU (Xenos plugin, Vulkan): `--native_live=off`. An explicit `--gpu_plugin xenos`
without `--native_live` also selects Xenos. `VK_ICD_FILENAMES` picks Xenos' GPU:

```sh
# NVIDIA (for the Intel one: intel_icd.json)
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/nvidia_icd.json \
./out/build/linux-amd64-relwithdebinfo/torchlight \
  --game_data_root $HOME/360tools/extracted/extracted --native_live=off
```

Parallel mode (diagnostics: the game keeps running on Xenos and every frame is also drawn by the
OGRE backend in a second window; the PRIME variables pick that window's GPU, GL):

```sh
__NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia \
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/nvidia_icd.json \
./out/build/linux-amd64-relwithdebinfo/torchlight \
  --game_data_root $HOME/360tools/extracted/extracted \
  --native_live=parallel --capture_dir $PWD/out/captures
```

F9 captures with the focus on either window.

MangoHud in only mode: the distribution's (Debian/Ubuntu) uses the system spdlog and clashes with
the one `librexruntime` exports (it breaks on the first swap). Use one built with its internal
spdlog, installed separately in `~/mangohud-install` (the system one is left alone):

```sh
mkdir -p ~/mangohud-build && cd ~/mangohud-build
python3 -m venv venv && ./venv/bin/pip install meson mako
# glslangValidator (used by MangoHud's Vulkan layer), built from the source shipped with the SDK
cmake -S ~/rexglue-sdk/thirdparty/glslang -B glslang -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_OPT=OFF -DGLSLANG_TESTS=OFF -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build glslang
git clone --depth 1 --branch v0.8.1 https://github.com/flightlessmango/MangoHud.git src
export PATH=$PWD/venv/bin:$PWD/glslang/StandAlone:$PATH
# -include sstream: gpu_fdinfo.cpp is missing the include with GCC 15
meson setup build src --prefix=$HOME/mangohud-install -Dbuildtype=release \
  -Duse_system_spdlog=disabled -Dwith_xnvctrl=disabled -Dmangoapp=false -Dmangohudctl=false \
  -Dmangoplot=disabled -Dtests=disabled "-Dcpp_args=-include sstream"
ninja -C build && meson install -C build
```

And its script is prepended to the command (it reads the same configuration as the system one):

```sh
~/mangohud-install/bin/mangohud ./out/build/linux-amd64-relwithdebinfo/torchlight \
  --game_data_root $HOME/360tools/extracted/extracted
```

Record the live mode session (every frame the backend consumed; cap `--live_record_max_mb`, 4096 by
default) and replay it offline with the accumulated state:

```sh
  ... --live_record=$PWD/out/sessions/session.tlses
./out/build/linux-amd64-relwithdebinfo/tools/replay/replay --session out/sessions/session.tlses \
  --game_data_root $HOME/360tools/extracted/extracted --out out/replay/session \
  [--session_frame N] [--session_frames A-B] [--region X0,Y0,X1,Y1] [--dump_targets]
```

To validate a change that must not alter the image (resource handling, caches), compare two replay
builds on one session: every sampled frame has to come out identical, and each one's time is
reported.

```sh
tools/replay/session_compare.sh REPLAY_BASE REPLAY_NEW out/sessions/session.tlses \
  $HOME/360tools/extracted/extracted 0-11000/29 6400
```

Tests (unit, without the game), after building:

```sh
ctest --test-dir out/build/linux-amd64-relwithdebinfo --output-on-failure
```

The tests never write to the user's folders: ctest sets `TORCHLIGHT_TEST_USER_FOLDERS` for each of
them to `out/build/<preset>/test_home/`, below which every user folder then lives (on every
platform; nothing else uses that variable), created empty before the suite and removed after it, and
`test_home_check` fails if the real `TorchlightRecomp` folders gained anything meanwhile (their
`logs/` aside, which a running game writes to).

The user's folders are named `TorchlightRecomp` (so they do not collide with the PC game's):
`~/.config/TorchlightRecomp/` (settings, the runtime's `torchlight.toml`),
`~/.local/state/TorchlightRecomp/logs/`, `~/.cache/TorchlightRecomp/` (shaders, layouts) and
`~/.local/share/TorchlightRecomp/` (saves, `save-backups`, profiles; the runtime's default
`--user_data_root`), or under the `XDG_*_HOME` directories. At startup, each folder still named
`torchlight` is renamed to `TorchlightRecomp` when the new one does not exist yet and the old one
holds this project's files; with both, nothing is touched. The log says what moved.

The logs go to `~/.local/state/TorchlightRecomp/logs/`, shared by every build and worktree: the
runtime's as `torchlight_NNN.log` and OGRE's next to it as `torchlight_NNN_ogre.log`. The game
prints the log's path at startup, and the first line of each log has the executable's path and the
build's commit (`git describe`, `-dirty` with local changes). `--log_file=PATH` puts the runtime's
log elsewhere. The runtime's own config, `torchlight.toml`, is read from
`~/.config/TorchlightRecomp/`. To quit, use Quit from the game's menu.

With an SDK without patch 15 (`patches/README.md`), a run that ends by a signal (a crash,
`timeout`, `kill`) leaves its guest memory behind as a `/dev/shm/xenia_memory_*` file of about
4.8 GB; enough of them fill the shared memory and later runs die with SIGBUS. To delete the ones no
running process maps (other instances keep theirs):

```sh
for f in /dev/shm/xenia_memory_*; do grep -qs "$f" /proc/[0-9]*/maps || rm -f "$f"; done
```

## Windows: the SmartScreen prompt

The Windows zip is not code-signed: signing needs a paid certificate tied to a verified identity,
and the beta goes without one. Windows' SmartScreen shows "Windows protected your PC" the first
time a program it has not seen downloaded often is started, and every new unsigned release starts
out that way. It is not a detection of anything harmful. To start the game, click **More info**,
then **Run anyway**; Windows remembers the choice for that copy. To check that a download is the
published one, compare its SHA-256 with the release's `SHA256SUMS`.

## Languages and translations

The game ships English plus German, French and Spanish. The language is chosen in the game's own
menu (Options → Settings, the *Video* column on the right, *Language*); the change applies after a
restart. The choice is saved with the other host settings in
`~/.config/TorchlightRecomp/settings.toml`.

### Language packs

Any other language can be added as a language pack: a folder `translations/<code>/` in the extracted
game data (next to `translations/de`, `fr` and `es`) with:

- `translation.dat.adm`: the translation, in the game's format (pairs of English original and
  translation). The game looks texts up by their exact English original.
- optionally `media/UI/`: fonts for the language's script. A pack's files take the place of the
  game's with the same path. Scripts the game's fonts lack (Cyrillic, for instance) need `.font`
  files pointing to a font that has them, including `Serif14.font`, which the Xbox 360 version has
  and the PC version does not.

Packs appear in the *Language* list by their folder name. If the selected pack is removed, the game
starts in English.

### Translations from the PC version

Translations made for Torchlight on PC use the same `translation.dat.adm` format and can be used as
packs. The official Russian translation of the GOG release, for instance, covers about 92% of the
Xbox 360 texts, with its fonts from the release's `Pak.zip` (`media/UI/*.font` and `ariblk.ttf`). To
take it from your own GOG installer without running it:

```sh
innoextract --language=ru-RU -I translations -I Pak.zip -d /tmp/tl_ru setup_torchlight_*_(russian)_*.exe
```

What a PC translation lacks is the text that only exists on the Xbox 360 (Xbox LIVE messages,
leaderboards, the controller tutorials, a few menus such as *New Character*) and texts whose English
changed between versions. Mods made for the PC version's mods folder (for instance translations that
replace game data files) are not supported.

Scripts written right to left (Arabic) are not supported. Chinese and Japanese are not supported yet.

### Completing a translation

`tools/translations/tl_translate.py` lists what a pack is missing and adds what someone translated;
it works on the game data directly and never runs the game:

```sh
# Write translations/ru/missing.txt with every text the pack has no translation for.
tools/translations/tl_translate.py missing --game-data ~/360tools/extracted/extracted --lang ru

# After filling it in: add the translated entries to the pack
# (the previous translation.dat.adm is kept as translation.dat.adm.bak).
tools/translations/tl_translate.py merge --game-data ~/360tools/extracted/extracted --lang ru \
  ~/360tools/extracted/extracted/translations/ru/missing.txt
```

The file is UTF-8, one entry per pair of lines, taken exactly as written after the prefix:

```
EN: New Character
TR: Новый персонаж
```

Entries left empty are ignored, so a file can be filled in bit by bit and merged again; `missing`
then lists only what is still left. `\n` in a text is the game's line break. Markers such as
`[VALUE]`, `[ITEM]` or `|cFFFFBA00…|u` must stay in the translation (in any order); `merge` skips
entries whose markers differ from the original's and says which. Starting a new language works the
same way: with no pack yet, `missing` lists all of the game's texts.

## License

- The project's own code is licensed under the GNU General Public License, version 3
  ([LICENSE](LICENSE)).
- The patches to the ReXGlue SDK in [patches/](patches/) are licensed under the BSD 3-Clause
  License ([patches/LICENSE](patches/LICENSE)), ReXGlue's license, so that they can go upstream.
  Patches taken from other projects keep their original license and attribution.
- The game is not included: you need your own copy of Torchlight for Xbox 360.
- The code generated from the game (`generated/`) is not covered by any of the project's licenses.
- Third-party licenses are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
