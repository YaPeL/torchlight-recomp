# Windows port: research

Goal: build and run the game on Windows (x64) with the native backend (`--native_live=only`), using
the same code as Linux. This is research only, from reading this repository, the ReXGlue SDK
checkout (`~/rexglue-sdk`) and OGRE 14's documented options. Nothing here has been built or run on
Windows.

Each area below gives its current state, whether it **blocks** a first playable Windows build, and
a bounded follow-up ticket (WIN.n). Tickets are listed in dependency order at the end.

## Summary

| Area | State | Blocks? |
|---|---|---|
| Toolchain | Presets for `win-amd64-*` exist (Ninja + clang); our CMake has Linux-only bits | Yes (small) |
| ReXGlue SDK | Upstream builds win-amd64 (CI); our 10 patches are untested there, 1 is POSIX-only | Yes |
| OGRE 14 GL3+ | Supported on Windows (WGL); draws on the game window's HWND | Yes (needs the build) |
| OGRE 14 D3D11 | Possible later; our own GLSL programs and a convention review are needed | No |
| Platform module | Only `platform_linux.cpp`; CMake stops on other systems | Yes |
| Settings and cache paths | Linux XDG paths inside the platform module | Yes (part of the platform module) |
| Window and input | SDL3 handles input; embedding needs the HWND | Yes (part of the platform module) |

## Toolchain

**State.** `CMakePresets.json` already has `win-amd64-debug`, `-release` and `-relwithdebinfo`
(Ninja, `clang`/`clang++`), inherited from the SDK template, and the root `CMakeLists.txt` builds
the executable with the `WIN32` subsystem on Windows. What is Linux-only today:

- `src/backend/CMakeLists.txt`: the OGRE install defaults to `$ENV{HOME}/ogre14-install`, links
  `libOgreMain.so` and `libOgreRTShaderSystem.so` by file name and sets an `rpath`. Windows needs
  the import libraries (`OgreMain.lib`, `OgreRTShaderSystem.lib`) and the DLLs copied next to the
  executable (no rpath on Windows).
- `src/platform/CMakeLists.txt` links `X11` and has no Windows branch (`FATAL_ERROR`).
- `find_package(ZLIB REQUIRED)` (frontend, live): there is no system zlib on Windows. Options are
  the zlib OGRE or the SDK already vendor, or a package manager; pick one so the build stays
  reproducible.
- `-Wall -Wextra` work with the `clang++` driver; they would not with `clang-cl`. The presets use
  `clang++`, so keep it.
- Tools: `tools/replay/session_compare.sh` is a bash script (Git Bash/MSYS is enough for a
  developer tool); `tools/translations/tl_translate.py` is plain Python.

clang on Windows targets the MSVC ABI and the MSVC C++ runtime by default, so OGRE and the SDK must
be built with the same runtime flavor (`/MD` vs `/MDd`) as the game, or the Debug build mixes CRTs.

**Blocks:** yes, but it is mechanical.

**Ticket WIN.1 (toolchain):** make the CMake configure on Windows: OGRE paths through
`TORCHLIGHT_OGRE_INSTALL` with a platform default, link by target or by `.lib`, copy the OGRE DLLs
and plugins next to the executable, choose the zlib source. Done when `cmake --preset
win-amd64-relwithdebinfo` configures with the platform module stubbed (WIN.4 not yet done) and the
pure unit tests (`commands`, `guest_abi`, `rtss`, `settings`, game_menu model) build and pass.

## ReXGlue SDK

**State.** The SDK is cross-platform upstream: it has `*_win.cpp` implementations for the core
(threads, memory, file system, exceptions, fibers), a `win-amd64` preset and a Windows CI workflow
for releases. The recompiled code (`generated/`) is plain C++ and does not depend on the host. The
Xenos plugin has a D3D12 backend on Windows besides Vulkan. Our patches (`patches/README.md`) were
only built on Linux:

- `rexglue-posix-wait-fraction.patch` only touches `threading_posix.cpp`: it does not apply to
  Windows and is not needed there, but whether the Windows timed waits also busy-poll was not
  checked.
- `rexglue-gpu-null-plugin.patch` adds a plugin target to the SDK's CMake and its install rules;
  the plugin is a DLL on Windows, so its exports and install path need a check. The original patch
  had a `std::jthread` workaround (dropped as not needed on Linux); MSVC's STL has `std::jthread`, so
  it should not be needed on Windows either.
- `rexglue-guest-vblank-rate.patch`: the vblank thread sleeps until the next vblank with at most
  1 ms per round. On Windows a plain sleep has about 15.6 ms granularity unless the process raises
  the timer resolution or uses a high-resolution waitable timer; the SDK's Windows threading code
  has no `timeBeginPeriod` or high-resolution timer flag. Without one the FPS cap and the guest's
  timing would be coarse. Needs a check on the real system; the fix, if needed, is in WIN.2.
- The rest (Vulkan fixes, XAM exit, ImGui dialogs, shutdown) are platform-neutral code and should
  apply as they are.
- `rex_app.cpp` is copied to the install's `share/rexglue/` (patch 2); the install step does that
  on every platform.

**Blocks:** yes: the game cannot run without an SDK build with patches 2, 7, 8, 9 and 10.

**Libraries in two modules.** On Windows `rexruntime` is a DLL built with
`WINDOWS_EXPORT_ALL_SYMBOLS`: it exports the functions of everything compiled into it (its own
code, ImGui, the cvar and log category registries), but not its global variables nor the static
libraries it links, and its package passes those static libraries on to the executable, which
gets its own copy. Checked on the executable's link line and symbols:
- SDL3 was one of them: two SDLs, the executable's empty (only mode's black screen). Patch 18 makes
  it a DLL on Windows; the package must ship `SDL3.dll` next to the executable.
- ImGui, the cvars and the log categories are not duplicated: the executable calls the runtime's
  exported functions (one ImGui context, one cvar registry; log categories registered by name).
- fmt is duplicated but holds no state.
- spdlog is duplicated: its registry and default logger exist twice, and the executable's are
  empty. It is harmless while nothing in the executable uses them: **the project logs only
  through the SDK's `REXLOG_*` macros** (they log to the runtime's own loggers, through
  `rex::GetLoggerRaw`), never through spdlog directly (no spdlog include, no `spdlog::get`, no
  `spdlog::info` and the other level functions, no default logger; `spdlog::level::` values are
  fine). The `no_direct_spdlog` test (`cmake/check_no_spdlog.cmake`) fails on any such use in
  `src/`. If a direct use were ever needed, the fix would be building spdlog and fmt as DLLs on
  Windows, as SDL3.

**Ticket WIN.2 (SDK on Windows):** build and install the SDK with `win-amd64` and our patches
(except the POSIX one); confirm the null plugin DLL loads, and measure the vblank thread's real
interval at 60 and 144 Hz. Done when the SDK installs and an unmodified recompiled title boots to
its first frame on Windows with `--gpu_plugin null`, with the measured vblank interval reported. If
the interval is coarse, the fix is Windows' high-resolution timers: a waitable timer created with
`CREATE_WAITABLE_TIMER_HIGH_RESOLUTION` (Windows 10 1803 and later) for the vblank thread's waits,
or `timeBeginPeriod(1)` for the process; it goes in as a patch to the SDK (`patches/`).

## OGRE 14: GL3+ and D3D11

**GL3+ state.** OGRE 14's GL3+ render system supports Windows through WGL, and the RTSS ships its
GLSL shader library on every platform. Window embedding uses the same `parentWindowHandle`
parameter as X11, with an `HWND`. Nothing in `backend/` calls GL directly (CLAUDE.md rule); the
backend loads `RenderSystem_GL3Plus` and `Codec_STBI` by plugin name, so on Windows only the file
naming changes (OGRE resolves the `.dll` suffix). The Wayland-only subdirectory
(`OgreRenderSystemDir`) has no Windows counterpart; the platform returns the plain plugin directory.

The build recipe in docs/BUILDING.md works on Windows with two changes: no Wayland build of the plugin,
and the install prefix. A pinned OGRE build described by the project (already a pending item in
`ARCHITECTURE.md`, cross-platform stage) would serve the four platforms.

**D3D11 state.** OGRE 14 has `RenderSystem_Direct3D11`, and the RTSS generates HLSL from the same
SubRenderStates: our own stages (`GuestTexgen`, `GuestPixelFog`, `GuestAlphaTest`,
`GuestLighting`) build their code through the RTSS API (`callFunction`, `callBuiltin`, FFPLib
dependencies), not through GLSL text, so they should carry over. What would not carry over:

- the backend's own programs (gamma ramp, presentation copy, host UI pass) are written as GLSL 330
  source strings in `backend.cpp`; D3D11 needs HLSL versions of them (or OGRE's unified shader
  dialect);
- `xbox_to_gl_conventions` encodes Xbox D3D9 → GL: depth range, render target Y flip and UV origin
  are different on D3D11 (depth 0..1 and top-left origin, closer to the Xbox). Some of it OGRE
  already handles per render system (projection conversion, render target flip); each convention
  has to be reviewed against the active render system, still in that single module;
- the shader cache directory, render system choice in settings (`render_system` already exists in
  `HostSettings` and the menu as a restart setting) and the plugin to load.

D3D11 would also give a GPU choice on Windows (DXGI adapters; see the platform module below), and
avoids depending on the quality of each vendor's OpenGL driver on Windows (Intel's in particular).

**Blocks:** GL3+ yes (it is the renderer); D3D11 no.

**Ticket WIN.3 (OGRE on Windows):** build OGRE 14.6.0 for `win-amd64` with the same options as
Linux (GL3+, RTSS, STBI) and the project's runtime flavor. Done when the replay tool runs on Windows
against the 20 reference captures and gives the same PSNR as on Linux (the replay needs no game,
so it validates the backend before the platform module is complete).

**Ticket WIN.7 (D3D11, in the first Windows beta, after WIN.6):** OGRE's D3D11 render system built
by `tools/build-deps/windows.ps1`, HLSL versions of the backend's own programs (gamma ramp,
presentation copy, host UI pass), the conventions that differ between GL and D3D (depth range,
half pixel, UV origin, render target flip) reviewed per render system in `xbox_to_gl_conventions`,
and `render_system = d3d11` offered in the Video column only once validated. Done when the 20
captures replay on D3D11 with the same result as on GL3+. (NVIDIA's overlay showed no FPS counter
in the first GL3+ run, WIN.5, and showed it in the next one, WIN.6, still GL3+: not an OpenGL
limit; perhaps the manifest, added in between, or how the overlay hooks the process. Not a D3D11
criterion.)

**WIN.7 done.** The backend takes the render system at creation (`tl_render_system`; the replay's
`--render_system gl3plus|d3d11`); its own programs are unified GLSL/HLSL sources
(`OgreUnifiedShader.h`), and the custom RTSS stages generate HLSL without changes (no OGRE error and
no skipped draw in the captures on D3D11). What differs between the render systems, found by
cutting draw by draw on D3D11:

- clears: D3D11 clears the whole target whatever the viewport (measured at creation; a clear of
  part of the target draws a quad, `ClearViewport`);
- constant depth bias: OGRE's D3D11 multiplies it by 10 (`conv::DepthBiasConstantForHost`);
- DXT mip levels smaller than 4x4: D3D11 uploads them wrong through OGRE; every render system now
  keeps only the levels of at least 4x4 (`conv::CompressedMipmapsForHost`), which also brought GL3+
  closer to the Xenos captures;
- `parentWindowHandle` makes the backend's window a child on both (the D3D11 plan expected an
  external window).

The 20 captures on D3D11 are within 0.1 dB of GL3+ against Xenos except v14 swap2211 (+0.30, the
lanterns' edges: NVIDIA's GL polygon offset unit is effectively twice D3D11's), and differ from
GL3+ only at edges. At the main menu (4K, 120 Hz, vsync) the process takes 89 % of one core on D3D11
against 168 % on GL3+ (the backend's thread 25 % against 87 %: NVIDIA's GL driver spins in its vsync
wait), with the same frame times, latency and dropped frames. A game run with D3D11 (vsync, Alt+Tab,
250 % scaling, the town) went fine. The Video column offers Direct3D 11 where its plugin is installed
(`platform::HasDirect3D11`, Windows), first, so it is the default there (automatic); GL3+ is the
other choice. Against the reference regenerated with the mip limit (main 943e37d), GL3+ on Windows
is identical pixel for pixel in the 20 captures, and Direct3D 11 is within 0.04 dB of it or better.
In the game too (4K, 120 Hz, vsync; the menu, then standing in a zone after Continue): the
process takes 88 % / 162 % of one core on Direct3D 11 against 167 % / 195 % on GL3+ (menu / zone),
with the same main thread, frame time p99 (about 10 ms), latency (29 ms mean, 33.5 p99), zone load
and dropped frames.

Without OpenGL 3.3 (no GPU driver: Windows' own OpenGL 1.1, as in Windows Sandbox or a CI runner)
OGRE's GL3+ crashes while creating its window, before its own version check
(`OgreGL3PlusRenderSystem.cpp:1390`), so a player who chose GL3+ could not start the game to change
it back. Before the backend starts, `platform::CanCreateGl33Context` (`gl_probe_win.cpp`) tries
the WGL sequence on a hidden window (a legacy context, `wglCreateContextAttribsARB`, a 3.3 core
context) and releases everything whatever step fails; `platform_win_test` makes each step fail and
checks that no window, window class, current context or GDI object is left. The startup logs the
result and its time (`live: OpenGL 3.3 available (N ms to check)`). Without it GL3+ is listed in
`Capabilities::unavailable_render_systems`: the session uses Direct3D 11
(`settings::EffectiveRenderSystem`, logged as a warning), `settings.toml` keeps the saved GL3+
(Normalize keeps it, and only choosing a renderer by hand changes it), and the Video column shows
"OpenGL 3+ (unavailable)", from which either arrow goes to Direct3D 11. Checked 2026-10-06 in
Windows Sandbox without its GPU (`sandbox_check.ps1 -NoGpu`), with GL3+ saved: the probe took
4.6 ms and found no OpenGL 3.3, the game reached the menu on Direct3D 11 (WARP), the Renderer row
showed "OpenGL 3+ (unavailable)", and after changing the frame rate limit and saving,
`settings.toml` still had GL3+. With the sandbox's GPU (the host's, shared) the probe found
OpenGL 3.3 and the game ran on GL3+.

With a GPU driver the probe takes 150-180 ms (NVIDIA's OpenGL driver loading; 4.6 ms on Windows'
own OpenGL), so since 2026-10-07 startup runs it only when the session would use GL3+ and could
fall back (`settings::StartupNeedsRenderSystemCheck`: GL3+ saved, or first offered, with Direct3D
11 installed). Otherwise startup logs `live: OpenGL 3.3 check deferred to the settings menu`, and
the check (`live::DeferredCheck`, at most once) runs when `live::HostCapabilities` is first asked:
the video column is built with the game's settings menu, once, while the title screen loads (game
thread, about 2.5 s after the backend starts), not when the player opens Settings. So the probe
left the backend's startup but still runs in every session, hidden in the title's load; moving it
to the first change of the Renderer row (the only thing that needs it when Direct3D 11 is saved)
would mean building the video column's model without it. Measured on a Ryzen 7 5700X3D with an
RTX 5080, Direct3D 11 saved, from the log's first line to `live: backend draws inside the game
window`, three runs each on fresh user folders: before 1588, 1177 and 1097 ms (the probe 264,
150 and 149 ms; the first run cold), after 1006, 949 and 940 ms, with the probe at 130 ms during
the title's load. With GL3+ saved the probe still runs at startup (168 ms, GL3+ loaded); with
Direct3D 11 saved the deferred check found OpenGL 3.3 when the settings menu was built, so the
Renderer row offers OpenGL 3+ (the user opened Settings and felt no pause).

Paths with accents and ñ (a Windows account such as Martín's): `utf8_paths_test` checks a
command-line argument both ways the executables read it (main's argv; GetCommandLineW converted to
UTF-8, the game's through the SDK), `user_folders_test` and `platform_win_test` the user folders
below such a base. A game run with `--game_data_root` on a copy of the data below
`D:\prueba-ñandú-copia` and `--user_data_root` below `D:\Usuarios\Martín Pérez` reached the menu
without the first-start setup, and saving wrote there and nothing in the default saves folder.
(`--user_data_root` is for testing; the settings stay in `%APPDATA%\TorchlightRecomp`.)

## Platform module

**State.** `src/platform/platform.h` is already the only interface the rest of the code uses for
window system and video details (the CLAUDE.md rule holds: no platform `#ifdef` or window system
include outside it; the only SDL/X11 includes are in `platform_linux.cpp`). A Windows port is a new
`platform_win.cpp` behind the same interface, plus the CMake branch. Per function:

| Function | Linux today | Windows |
|---|---|---|
| `NativeWindow` | X11 id or Wayland surface + display | New `kWin32` system: the `HWND` (and the backend's `tl_native_window` gains the same value) |
| `EmbeddingVideoError`, `VideoDriver` | x11 or wayland required | SDL's `windows` driver; always embeddable |
| `BlackGameWindowBackground` | X11 background via an event watch | Likely a no-op (SDL clears its Win32 window); verify there is no white flash |
| `FindGameWindow` | SDL properties for X11/Wayland | `SDL_PROP_WINDOW_WIN32_HWND_POINTER` |
| `GameDisplay`, `AllowBackgroundGamepad`, `ReadGamepad` | SDL3 | Same SDL3 code (it is not Linux-specific; could move to a shared `platform_sdl.cpp`) |
| `OgreWindowParams` | `parentWindowHandle` / `externalWl*` | `parentWindowHandle` with the HWND |
| `OgreRenderSystemDir` | Wayland subdirectory | The plugin directory |
| `KeyReader` (replay window F9) | X11 key events | Win32: read the window's key messages, or return null (F9 in the replay window is a diagnostic convenience) |
| `Gpus`, `SelectableGpus`, `GpuEnvironment`, `SelectGpu` | DRM + pci.ids + PRIME variables | See below |
| `ExecutableDir` | `/proc/self/exe` | `GetModuleFileNameW` |
| `ConfigDir`, `ShaderCacheDir` | XDG paths | See the next section |

GPU choice on Windows: there are no driver variables like PRIME. For GL3+ the only per-process lever
is exporting `NvOptimusEnablement` and `AmdPowerXpressRequestHighPerformance` from the executable
(it asks for "the high performance GPU", not a specific one, and it is decided at load, so it cannot
follow a setting without a restart and a second executable). The user can also pick the GPU per
application in Windows' graphics settings. With D3D11 the backend can enumerate DXGI adapters and
pass one to OGRE, which is a real choice. For a first version the GPU row can stay disabled on
Windows (the model already disables a row with a single option), with the setting kept for D3D11.

**Blocks:** yes.

**Ticket WIN.4 (platform module on Windows):** `platform_win.cpp` with everything in the table
except GPU choice (one boot GPU reported), the `kWin32` window kind in `platform.h` and
`backend_api.h`, and the CMake branch. SDL3 code shared with Linux where it is identical. Done when
`gpu_choice_test` and the platform-independent tests pass on Windows and the replay with `--window`
presents in an OGRE top-level window.

**Ticket WIN.8 (GPU choice on Windows, after WIN.7):** DXGI adapter enumeration for D3D11 and the GPU
row enabled only when the render system can honor it. Done when choosing an adapter in the Video
column changes the adapter OGRE reports after a restart.

**WIN.8 done.** What decides the GPU on Windows, and when:

- **Automatic (the default, an empty `gpu`).** The backend leaves OGRE's "Rendering Device" at
  "(default)": OGRE's Direct3D 11 then creates its device on DXGI's first adapter (EnumAdapters1
  order), and OpenGL gets the GPU Windows gives the process. Windows puts first the adapter of the
  primary display, or the one the user set for the application in Windows' graphics settings.
- **`NvOptimusEnablement` and `AmdPowerXpressRequestHighPerformance`**, exported by the executables
  (`platform_win.cpp`): on a machine with an integrated and a dedicated GPU, NVIDIA's Optimus and
  AMD's switchable graphics drivers read them when the process starts and run it on the dedicated
  GPU, for OpenGL as for Direct3D. It is OpenGL's only lever on Windows. Windows' per-application
  setting, when there is one, wins.
- **The Video column (Direct3D 11 only).** The GPUs are DXGI's hardware adapters, listed in the high
  performance order (IDXGIFactory6::EnumAdapterByGpuPreference, Windows 10 1803 and later;
  EnumAdapters1 order before), "Auto" first. The setting keeps a stable id (`pci:VVVV:DDDD:
  SSSSSSSS:RR:N`, the PCI ids plus an ordinal among identical adapters) and the name, the name only
  to show and log. The backend resolves the id against the devices OGRE's render system itself
  offers, crossed with DXGI's enumeration (`platform::RenderingDeviceForGpu`,
  `gpu_adapters.h`); without a match it stays automatic, said in the log. With OpenGL the row is
  disabled (the setting is kept for Direct3D 11), and with one GPU too; with only software adapters
  (no GPU driver) the list is empty and the game starts automatic.
- **WARP** (the software adapter) is never offered to the player: only the replay reaches it
  (`--list_gpus`, `--gpu ID`), for testing.

Validated on one GPU (RTX 5080): the list (the 5080, and WARP for the replay), the 20 captures on
Direct3D 11 identical with automatic and with the 5080 chosen, a capture set on WARP (3D within 0.1
dB against Xenos except v19town swap2018, +0.40; the 2D menu 56.2 dB instead of 69.8, mean error
0.14: WARP's own rounding), an unknown id falling back to automatic, and the game with a GPU stored
that is present (one GPU: automatic) or gone (automatic, warned). Pending: machines with an
integrated and a dedicated GPU (beta testers), where the Optimus/PowerXpress exports and the
choice in the Video column are to be checked.

## Settings and cache paths

**State.** The paths are already behind the platform module: `ConfigDir()` (settings.toml) and
`ShaderCacheDir()` (OGRE RTSS shaders) resolve XDG directories on Linux; `ExecutableDir()` finds
`data/ui` next to the executable. The game's saves are not ours: the SDK decides where the save
device lives on each platform (`~/.local/share/TorchlightRecomp/...` on Linux).

Windows conventions: settings in `%APPDATA%\TorchlightRecomp\` (roaming, small) and the shader
cache in `%LOCALAPPDATA%\TorchlightRecomp\ogre\` (machine-specific, can be large); not `torchlight`,
the folder of Torchlight for PC (`%APPDATA%\runic games\torchlight`). Resolved with
`SHGetKnownFolderPath` (`FOLDERID_RoamingAppData`, `FOLDERID_LocalAppData`). Paths must go through
wide strings (UTF-16) to support user names outside the ANSI code page; the settings code and OGRE
take UTF-8 `std::string`, so the platform converts once (`platform_win.cpp`, WIN.4b). That is only
half of it: a UTF-8 `std::string` turned into a `std::filesystem::path`, or passed to OGRE's and the
CRT's narrow file functions, is read in the ANSI code page, so a user name with accents or an `ñ`
breaks every path under it. The fix is the process code page: an application manifest with
`<activeCodePage>UTF-8</activeCodePage>` (Windows 10 1903 and later) makes the narrow APIs UTF-8.
It goes in with the DPI manifest (WIN.6) and **blocks the Windows beta**.

The settings file's atomic write (`settings.toml`, write and rename) needs a replace that works when
the target exists: `std::filesystem::rename` does that on Windows with the MSVC STL (it uses
`MoveFileExW` with `MOVEFILE_REPLACE_EXISTING`), so it should hold; the test covers it once it runs
on Windows.

**Blocks:** yes, as part of WIN.4 (no separate ticket): settings and shader cache use these paths.

## Window and input

**State.** The game's window is SDL's (the SDK's `ReXApp`), and input (keyboard, gamepad, the F9
bind) goes through SDL to the guest; that part is the SDK's and is cross-platform. Our side:

- the backend draws inside the game's window as a child window (X11) or on its surface (Wayland).
  On Windows there is no child window either: OGRE 14's Win32 GL window takes a
  `parentWindowHandle` as its own window, the same as an `externalWindowHandle`
  (`OgreWin32Window.cpp`, the `parentWindowHandle` option), sets its pixel format on SDL's `HWND`
  and draws on it with WGL from the backend's thread; it never destroys it (external windows are
  left alone). SDL creates that window without `SDL_WINDOW_OPENGL` or `SDL_WINDOW_VULKAN`, so no one
  sets a pixel format before OGRE, and the `null` GPU plugin has no presenter drawing on it. The
  only window is SDL's, whose messages SDL's UI thread pumps: nothing to pump on the backend's
  thread, no input queues shared between threads, and clicks land on SDL's window, which keeps the
  keyboard focus;
- fullscreen: the SDK's `fullscreen` cvar uses SDL's borderless fullscreen desktop mode on every
  platform; the backend follows the window's size (it already resizes on size changes);
- vsync: GL3+ on Windows uses `WGL_EXT_swap_control`, through OGRE's `vsync` option, as on Linux;
- DPI: Windows scales non-DPI-aware windows by bitmap stretching. The executable needs a manifest
  that declares per-monitor DPI awareness (SDL3 sets it at runtime too), so the window size in
  pixels is real and the runtime dialogs (ImGui) scale correctly, as they do at 150 % on Linux;
- the replay tool's F9 in its own window goes through `KeyReader` (WIN.4).

Alt-tab with a borderless fullscreen window on Windows keeps presenting (unlike Wayland's hidden
surface, which blocks the swap), so the backend pause described in `ARCHITECTURE.md` does not apply.

**Blocks:** drawing on the game window yes (part of WIN.4); the DPI details are polish.

**Ticket WIN.5 (game on Windows, first run):** the game in only mode on Windows: boot to the main
menu, play, F9 capture, settings saved in `%APPDATA%`, Quit exits cleanly. Done when the run reaches
the town with sound and the F9 capture replays on Linux with the same result as on Windows (the
capture format is platform-neutral, so this is a cross-check of the whole pipeline).

**Ticket WIN.6 (window polish):** an application manifest with per-monitor DPI awareness and
`<activeCodePage>UTF-8</activeCodePage>`; no white flash at startup. The UTF-8 code page **blocks
the Windows beta**: user names with accents or `ñ` are common.

The manifest (`src/platform/torchlight.manifest`) is a source of every Windows executable (game,
replay, tests), which the linker merges into the manifest it embeds. `utf8_paths_test` checks the
UTF-8 code page and the DPI awareness, and that a UTF-8 `std::string` names a folder and a file
correctly through `std::filesystem`, `fopen` and `std::ifstream` (read back with the wide API);
built without the manifest it fails (the folder comes out as `Ã±andÃº`). Done when, besides, a run
of the game with its game data, user data and captures under paths with `ñ` reads and writes them,
and at the display's scale (250 % on the test machine) the runtime dialogs' clicks land where they
are drawn. Optional, not needed for the beta: a run under a Windows account whose name has an `ñ`
(the user folders themselves under such a name).

**Done** (Windows 11, RTX 5080, 4K at 250 % scale): the game ran with its game data, user data and
captures under `D:\prueba-ñandú\` (data read, saves written, the save's `backup.tmp` rename
included, an F9 capture written and replayed: 214 of 214 draws); the XAM keyboard's clicks landed
where it is drawn at 250 % (as on Linux); no white flash at startup.

## Windows package

The zip is made like the Linux AppImage, from a `cmake --install` tree:

```bat
cmake --preset win-amd64-release -DCMAKE_PREFIX_PATH=<sdk>/out/install/win-amd64 "-DCMAKE_CXX_FLAGS_RELEASE=-O3 -DNDEBUG -g"
cmake --build --preset win-amd64-release
cmake --install out/build/win-amd64-release --prefix INSTALL
powershell -File packaging/windows/make_zip.ps1 INSTALL Torchlight-Recomp-VERSION-x86_64.zip
powershell -File packaging/windows/check_zip.ps1 Torchlight-Recomp-VERSION-x86_64.zip
```

Release takes OGRE's RelWithDebInfo libraries (`tools/build-deps/windows.ps1` builds no Release;
same runtime). The symbols (`.pdb`) stay in the build tree; `make_zip.ps1` refuses them.

- **MSVC runtime: app-local.** `vcruntime140.dll`, `vcruntime140_1.dll`, `msvcp140.dll` and
  `msvcp140_atomic_wait.dll` (the ones the binaries import), from the Build Tools' redistributable
  folder (`VC\Redist\MSVC\14.44.35112\x64\Microsoft.VC143.CRT`), installed next to the executable
  by `InstallRequiredSystemLibraries` (told the toolset and architecture clang++ simulates: the
  module only runs for cl). Microsoft's Distributable Code. The C runtime (UCRT, `api-ms-win-crt-*`)
  and `D3DCompiler_47.dll` come with Windows 10 and later. A static runtime (/MT) does not fit: the
  game, the SDK and OGRE are DLLs that pass C++ objects to each other, and each would get its own
  runtime and heap.
- **zlib 1.3.2** (Zlib licence), built from source by `tools/build-deps/windows.ps1` (pinned by
  SHA-256), static: inside `torchlight.exe` (inflate and deflate) and `Codec_STBI.dll` (deflate).
  No zlib DLL.

What the zip carries (`check_zip.ps1` on the zip of windows-port with main merged, `82b4a33`
plus the notices: 27 MB, a `TorchlightRecomp\` folder; every component, its version and licence in
THIRD_PARTY_NOTICES.md, "Windows zip"):

| File | From | Licence |
|---|---|---|
| `torchlight.exe` (78 MB; zlib and SHA-256 inside) | this project | the project's; zlib and SHA-256: Zlib |
| `rexruntime.dll`, `rexgpu-null.dll`, `rexgpu-xenos.dll` | ReXGlue SDK, Release (no Tracy) | the SDK's (THIRD_PARTY_NOTICES.md) |
| `SDL3.dll` | the SDK (patch 18) | Zlib |
| `OgreMain.dll`, `OgreRTShaderSystem.dll`, `ogre\plugins\` (`RenderSystem_GL3Plus`, `RenderSystem_Direct3D11`, `Codec_STBI`), `ogre\media\` (Main, RTShaderLib) | OGRE 14.6.0 | MIT (inside `Codec_STBI`: stb, MIT or public domain; zlib) |
| `vcruntime140.dll`, `vcruntime140_1.dll`, `msvcp140.dll`, `msvcp140_atomic_wait.dll` | MSVC 14.44.35112 redistributable | Microsoft Distributable Code |
| `data\ui\` | this project | the project's |
| `THIRD_PARTY_NOTICES.md` | this project | the notices and licence texts the components require |

From Windows, not in the zip: kernel32, user32, gdi32, advapi32, shell32, ole32, imm32, winmm,
version, setupapi, ws2_32, bcrypt, dxgi, d3d11, d3dcompiler_47, opengl32 and the api-ms-win-* API
sets. `check_zip.ps1` fails on a DLL that is neither in the zip nor Windows' (the MSVC runtime
counts as missing even where this machine has it installed), on symbols and on debug builds. The
unpacked zip runs here (Direct3D 11, 120 fps at the menu). On a clean Windows (Windows Sandbox,
without Visual Studio or redistributables): `packaging/windows/sandbox_check.ps1 PACKAGE.zip`
opens the sandbox with the zip's folder mapped read-only, unpacks it on the sandbox's desktop and
starts the game. The zip of `b534c8f` passed (2026-10-06): no "... was not found" dialog for a DLL,
and the first start's setup asked for the XBLA package. `-GamePackageDir FOLDER` also maps,
read-only, the folder holding the user's XBLA package (the sandbox maps folders, not files) as
`game-package` on the sandbox's desktop, for the whole first start inside the sandbox; without it
there is no second mapping. The sandbox passes no USB devices, so a gamepad does not reach the game
there: `-KeyboardInput` starts it with the SDK's keyboard controller emulation
(`--mnk_mode=true`) and leaves `Torchlight (keyboard).cmd` on the sandbox's desktop to start it
again that way. The logon steps go to the sandbox as a script in a read-only folder of their own.
With the host's GPU shared (vGPU, the default where the host allows it) the sandbox has the host's
OpenGL 3.3, so the GL3+ fallback does not trigger there (checked 2026-10-06: "OpenGL 3.3
available"); `-NoGpu` turns the vGPU off, leaving Windows' own OpenGL 1.1 and WARP, as on a
machine without a GPU driver.

## Risks

- Vendor OpenGL drivers on Windows vary more than on Linux (Mesa); Intel's GL on Windows is the
  weakest. D3D11 (WIN.7) is the mitigation.
- The vblank and wait timing (WIN.2) decide whether the FPS cap and the guest's pacing are smooth;
  a patch to the SDK may be needed.
- The CRT flavor must match between OGRE, the SDK and the game, or Debug builds fail at runtime in
  ways that are hard to diagnose.
- Antivirus heuristics sometimes flag recompiled executables with large generated code sections;
  nothing to do in code, worth knowing when testing.

Pending checks found in the first runs (WIN.5):
- 3 % of the frames dropped as "backend behind" at 4K and 120 Hz (629 of 21205, frame end 3.5 to
  6 ms, vsync on, FPS cap 60): to review together with vsync and the FPS cap. The same 30 frames
  every 10 s on GL3+ and D3D11 at the menu, with the backend's frame at 8.49 ms (117.8 Hz) rather
  than 8.33: perhaps the driver presenting below 120 Hz (G-SYNC with vsync), not checked yet.
  After the first release: the drops themselves (bursts of a few frames while playing).
- A brief ghosting seen while playing, at the moments frames drop: the monitor's variable refresh
  (G-SYNC); gone with G-SYNC off. Not the live snapshot or program caches (the same with both
  reverted), and the frame queue never presents an older frame after a newer one.
- About 20 % of the CPU in use (some 3 of 16 threads) while the GPU is almost idle. On Linux the
  guest's main thread was seen spinning at 100 % while it waits for the vblank: measure the CPU per
  thread (guest, backend, audio, the SDK's) and replace busy waits by sleeping ones without changing
  the behaviour (it matters for the Steam Deck's battery). Measured on Windows at the main menu (4K,
  120 Hz, vsync) with `tools/thread_profile` (`thread_cpu`, `thread_sample`: CPU per thread and the
  busiest threads' call chains, from outside the process; Linux: `profile_utilization.py`,
  `profile_guest.py`): about 2 cores. The guest's main thread spins at ~100 % in `sub_821A5C10`
  calling `sub_82774170`, which polls the GPU's progress pointer between `db16cyc` no-ops (done:
  `hooks/gpu_wait_hooks.cpp` sleeps 200 us on every wait step: Windows main thread from 98.7 %
  to 37 %, Linux from 100 % to 68 %, with the same frame times, latency and zone loads; sleeping
  only when there was no progress, the first version, never slept on Linux); the backend's thread
  ~85 %, almost all inside NVIDIA's OpenGL driver (spinning in its vsync throttling, below
  `GL3PlusRenderSystem::_createVao`), plus ~20 % in the driver's present thread: to compare with
  D3D11 in WIN.7; the SDK's `TimerQueue` 5-10 % (a spinning wait strategy).

## Follow-up tickets, in order

1. **WIN.1 Toolchain:** CMake configures on Windows; pure unit tests pass.
2. **WIN.2 SDK on Windows:** patched SDK built and installed; null plugin boots; vblank interval
   measured.
3. **WIN.3 OGRE on Windows:** OGRE 14.6.0 built; the 20 captures replay with the Linux PSNR.
4. **WIN.4 Platform module:** `platform_win.cpp`, `kWin32` window, Windows paths; replay `--window`.
5. **WIN.5 First game run:** only mode to the town; F9 capture cross-checked on Linux.
6. **WIN.6 Window polish:** DPI, startup flash; UTF-8 code page (blocks the beta).
7. **WIN.7 D3D11 (first beta):** OGRE's D3D11 build, HLSL own programs, conventions per render
   system, selectable once validated.
8. **WIN.8 GPU choice (first beta, after WIN.7):** DXGI adapters in the Video column.
9. **After the beta: the guest's depth bias semantics.** The backend passes the constant bias OGRE
   requested, in the same units on both render systems (`conv::DepthBiasConstantForHost`). The
   guest's own `_setDepthBias`, with its inverted depth, is not reproduced; converting the bias
   from it would change GL3+ too.
10. **After the beta: Linux without OpenGL 3.3.** `platform::CanCreateGl33Context` returns true on
    Linux without probing (GL3+ is the only render system there), so without OpenGL 3.3 the game
    crashes in OGRE instead of telling the player why.
11. **Done 2026-10-07: `ui_pass_test` and `render_scale_test` on Direct3D 11 on Windows.** They take
    the render system from their command line (`backend/test_render_system.h`, also `--gpu=ID`)
    and are registered twice on Windows: GL3+ (label `opengl33`, left out on the runners) and
    `<name>_d3d11`, which the runners run on WARP. Checked here on GL3+, on Direct3D 11 with the
    GPU, and on Direct3D 11 forced to WARP (`--gpu=` the Microsoft Basic Render Driver's id).

## Integration into main

`windows-port` against `origin/main` (`git cherry`), for whoever merges it; nothing is merged from
here.

- Already in main with the same change: `7987a8e` (the first GPU wait hook) and `eb568e4` (the
  hook that sleeps on every step, main's `dacae4d`).
- In main with another change id: `fa988eb` (the mip limit) is main's `943e37d`, a cherry-pick that
  had a conflict in `xbox_to_gl_conventions.cpp` around `RenderTextureCopyQuad`.
- Not in main: the rest of the branch, from `9d56ce8` (gitattributes) to the latest docs commit:
  the patches 17 and 18 and their notes, `tools/build-deps/windows.ps1` (with Direct3D 11), the
  platform module for Windows (`platform_win.cpp`, `user_folders_win.cpp`, the manifest, DXGI GPUs
  in `gpu_adapters.*`), `tools/thread_profile`, the spdlog check, the backend's render system choice
  and unified GLSL/HLSL programs, the viewport clears, the Direct3D 11 depth bias, the render
  system and GPU in the settings and the Video column (Direct3D 11 the default on Windows), the
  replay's `--render_system`, `--gpu` and `--list_gpus`, and the tests with accents and ñ.
- Known conflicts, from a trial merge of `windows-port` into `origin/main` (not committed): four
  hunks, in `xbox_to_gl_conventions.h`, `xbox_to_gl_conventions.cpp` (two) and
  `conventions_test.cpp`. In all four main's side is empty and `windows-port` adds next to the mip
  limit (`DepthBiasConstantForHost` and `kD3D11DepthBiasFactor`, `RenderTextureCopyQuad`, the depth
  bias test): keep `windows-port`'s side.
- `tl_backend_create` and `tl_backend_create_child` take the render system and the GPU first:
  callers added in main after this list need both arguments.
- Since main took `windows-port` (`7df5df4`) and `windows-port` took main back (`82b4a33`): the
  Windows zip's notices and install rule, `sandbox_check.ps1 -GamePackageDir`, patch 18 in
  `patches/series`, the SDK in `tools/build-deps/windows.ps1`, `windows_toolchain.ps1`,
  `key.sh windows`, the Windows CI and release jobs, `collect_symbols.ps1`, the zip fixtures
  compared by entries in `test_save_convert.py`, and the GL3+ fallback without OpenGL 3.3. **For
  the render agent to review:** that fallback changes `game_menu/video_menu_model.cpp` (the
  Renderer row shows an unavailable saved choice, and an arrow goes from it to one that runs) and
  `settings/host_settings.*` (`unavailable_render_systems`, `EffectiveRenderSystem`, the choices
  without unavailable ones). **Also for the render agent:** the workflows' Linux jobs changed too:
  the GitHub actions moved to their Node 24 majors (`checkout@v7`, `cache@v6`, `cache/restore@v6`,
  `upload-artifact@v7`, `download-artifact@v8`, `attest-build-provenance@v4`), `ubuntu-latest` is
  pinned to `ubuntu-24.04`, and `tools/deps/key.sh` hashes the build system's name (the Linux key
  changes once; the cache keys spell it). And patch 19 (`rexglue-mnk-keystrokes.patch`, every
  platform): the SDK's keyboard controller emulation makes keystrokes, sharing the SDL driver's
  keystroke table in a new header, so the SDL input driver changes too (same behavior,
  `patches/README.md` item 19).
- The deferred OpenGL 3.3 check (branch `perf/deferred-gl-probe`, 2026-10-07). **For the render
  agent to review:** `live/install.cpp` (startup probes only when `StartupNeedsRenderSystemCheck`
  says so; `HostCapabilities` runs the deferred check on its first call, which the video column
  makes when the game builds its settings menu), the new `live/deferred_check.h` with its test, and
  `settings/host_settings.*` (`StartupNeedsRenderSystemCheck` and its test). `game_menu/` is
  unchanged.
