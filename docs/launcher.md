# Launcher: survey and proposal (2026-10-08)

Survey only: no code. Question: what should replace the first start's dialogs (the XBLA package
and the achievement set, `src/game_setup/`) with a launcher of our own that installs the game's
files, sets the options, manages mods and starts the game; on Linux (Steam Deck included), Windows,
macOS and, eventually, Android; with mouse and keyboard, a gamepad and a touch screen.

Sources are cited per statement. What could not be checked is marked **[not verified]**. The
first start and the game's menus are the render agent's area: whatever this proposal changes there
is for their review.

## 1. What we have today

**Bug in v0.1.0-beta (found 2026-10-08): the install runs without a progress bar.** The SDK builds
SDL with `SDL_RENDER` off (its `thirdparty/CMakeLists.txt`), so `SDL_CreateRenderer` always
returns `nullptr` and `platform::ProgressWindow::Open` gives up: the package is extracted and
checked with no window on screen until it finishes or fails. Nothing else goes wrong. Fixed by
SDK patch 21 (`rexglue-sdl-software-renderer.patch`: `SDL_Renderer` with its software driver
only), which stage 1 needs anyway. **For the next release's notes.**

- **First start** (`docs/ARCHITECTURE.md`, `src/game_setup/`): before the runtime's window exists,
  SDL3 message boxes ask for the XBLA package or an extracted folder (the system's pickers,
  `platform::PickFile`/`PickFolder`), the package is checked (LIVE/CON/PIRS, Arcade title
  `0x58410A7E`) and extracted with the runtime's STFS reader into `game.partial/`, every file
  checked against `game_files_table.inc` (size and SHA-256), then renamed to `game/`. A small SDL
  window was meant to show the progress with SDL's debug font (ASCII only), but it never opens
  (the bug below). Then a message box asks for the
  achievement set (Xbox 360 or PC). Texts: English templates translated from
  `data/ui/tl_setup_strings.txt` (de, fr, es), `setup_text_test` checks every language has every
  text. Lines are broken for SDL's boxes by `platform/text_wrap.h`.
- **Options**: the Video column in the game's own settings menu (`game_menu/video_menu.cpp`,
  model `video_menu_model.cpp`: render system, GPU, resolution, aspect, frame rate cap, vsync,
  language, achievements), saved in `settings.toml` (`settings/host_settings.h`). Changes of render
  system, GPU, language, aspect or achievements need a restart.
- **PC save import**: from the game's "load character" menu, reading `<game data>/import/`
  (`game_menu/save_import_menu.cpp`, `docs/saves-research.md`).
- **Mods** (branch `feature/pc-mods`, `docs/mods.md` there; not in `develop` yet): PC's layout in
  `DataDir()/mods/` (one folder per mod with `mod.dat` and `media/`), priorities in `mods/mods.dat`
  as PC writes it (a negative priority disables a mod), a backup of the saves at startup when the
  set of mods changed (`save-backups/<UTC>-mods/`), the mod manager built by the host before the
  guest runs (`src/mods/`, `game_menu/mods_install.cpp`). The main menu's mod warning cannot be
  shown on Xbox (an empty `Text`); "a later menu could edit `mods.dat`; not in the first version"
  (`docs/mods.md` section 8).
- **UI already in the process**: the SDK's Dear ImGui, and our runtime dialogs in only mode drawn
  by the backend (`live/ui_overlay.h`), navigated with the gamepad through `live/ui_gamepad.h`
  (A activates, B cancels, the stick as the d-pad; tested). SDL is 3.4.14 (the SDK's, a DLL on
  Windows).
- **Images of the game the user has, only after the install**: the package's extraction leaves
  `MarketplaceBanner.png` (420x95: the logo on the left, the heroes on the right), `TitleIcon.png`
  and `DashboardIcon.png` (64x64, the shield "T") in `game/`, next to `pak.zip` with the game's own
  art (for example the loading screen and the three class intros, 1024x725 regions of 1024x1024
  DXT1 textures under `media/ui/`). On the first start none of it exists yet: it is what the
  launcher comes to install.

## 2. What other recomps and ports do

| Project | UI technology | Separate launcher? | Gamepad | Touch | Mods UI | Platforms |
|---|---|---|---|---|---|---|
| Unleashed Recompiled | Dear ImGui with its own MSDF fonts and shaders, SDL2 | No: an install wizard before the game, options over it | own cursor from SDL events (wizard); the game's pad state (options) | no | none (external Hedge Mod Manager) | Windows, Linux/Flatpak |
| MarathonRecomp | as Unleashed | No | as Unleashed | [not verified] | [not verified] | Windows, Linux, macOS |
| RecompFrontend (Banjo; Zelda on a branch) | RmlUi (RML/RCSS) on RT64/plume | No: a launcher screen, then the game, same window | SDL2 pad events turned into RmlUi key events, focus navigation | none found | toggle, dependencies, drag to reorder, per-mod config, drop to install | Windows, Linux (x64, ARM64, Flatpak), macOS |
| Zelda64 / MK64 / SF64 Recomp | RmlUi, `.rml` in the tree | No | same model | none found | Zelda: yes; MK64/SF64: a mods tab | Windows, Linux, macOS |
| Shipwright / 2Ship / Starship | Dear ImGui (libultraship) | No: an extractor dialog, then in-game menus | ImGui's `NavEnableGamepad`, opt-in | Android builds only scale ImGui 2x | ImGui list, drag for priority (SoH) | Windows, Linux, macOS, Switch |
| NocturneRecomp (ReXGlue) | the SDK's ImGui overlays | optional: Goopie Launcher | [not verified] | [not verified] | `mods/mods.toml` only | Windows, Linux |
| Goopie Launcher (ReXGlue games) | Tauri 2 (Rust), a web UI | Yes, a separate process | [not verified] | [not verified] | ordered enable list passed as `--enabled_mods` | Windows, Linux |

Details:

- **Unleashed Recompiled** (<https://github.com/hedge-dev/UnleashedRecomp>): install wizard pages
  `SelectLanguage, Introduction, SelectGameAndUpdate, SelectDLC, CheckSpace, Installing,
  InstallSucceeded, InstallFailed` (`UnleashedRecomp/ui/installer_wizard.cpp`); files picked with
  nativefiledialog-extended, containers (ISO, STFS) or folders detected by content, every file
  checked by XXH3 against generated tables (`install/installer.cpp`, `install/hashes/`), free space
  checked; the wizard can be reopened to add DLC. Options and achievements menus are drawn over the
  game (`ui/options_menu.cpp`, `ui/achievement_menu.cpp`), navigated by the game's own pad state
  with prompts that switch between pad and keyboard. Texts in a compiled table
  (`locale/locale.cpp`, six languages). Launcher art from a separate resources submodule, nothing
  of the game in the repository (`README.md`). Steam Deck: install in Desktop Mode, "the file picker
  may not be available in Game Mode" (`README.md`).
- **RecompFrontend** (<https://github.com/N64Recomp/RecompFrontend>): `recompui`, "a comprehensive
  UI API built on RmlUi", and `recompinput` (SDL2 pad, keyboard, mouse, profiles). Launcher
  `recompui/src/base/ui_launcher.cpp` (start, load ROM, controls), configuration tabs (general,
  graphics, sound, controls, mods), mod menu with dependencies, drag to reorder, drop to install,
  "open mods folder" (`composites/ui_mod_menu.cpp`). The pad drives RmlUi's focus navigation
  (`base/ui_state.cpp`). No translations (English strings in the code). ROM validation in
  N64ModernRuntime (`librecomp/src/recomp.cpp`, XXH3). Launcher art is the projects' own vector
  art (for example `assets/mm-clipped.svg` in Zelda64Recomp), nothing from the ROM. Zelda64Recomp
  uses RmlUi "for building the menus and launcher", with "mouse, controller, or keyboard"
  (<https://github.com/Zelda64Recomp/Zelda64Recomp>).
- **Harbour Masters** (<https://github.com/HarbourMasters/Shipwright>, libultraship
  <https://github.com/Kenix3/libultraship>): no launcher; an extractor that finds the ROM or asks
  for it (`soh/soh/Extractor/Extract.cpp`, CRC32 against known versions), then ImGui menus in the
  game (`src/ship/window/gui/Gui.cpp`: gamepad navigation behind a setting; on Android the style
  and fonts scaled 2x). SoH's mod menu lists enabled and disabled packs, "Mod priority is top to
  bottom", applied on restart (`soh/soh/Enhancements/mod_menu.cpp`).
- **NocturneRecomp** (<https://github.com/birabittoh/NocturneRecomp>): "run the executable and it
  will prompt you to extract the game" (`README.md`; where that prompt lives was **[not verified]**);
  mods as folders ordered by `mods/mods.toml` (`docs/making-mods.md`). **Goopie Launcher**
  (<https://github.com/birabittoh/GoopieLauncher>): a separate Tauri app for ReXGlue games; extracts
  ISO or STFS, checks `default.xex` by SHA-256 with rollback, can link an extracted folder instead
  of copying it, keeps the mod order in a `mods.toml` and passes `--enabled_mods` to the game,
  writes Steam shortcuts.
- **Contrast**: Dolphin (Qt), Ryujinx (Avalonia) and Xenia (a native window with ImGui) are
  desktop launchers, not console-style UIs [not verified in detail].

What follows from it:

1. Every recomp with an install wizard runs it **in the game's process, before the game, in the
   same window**. Only Goopie is a separate program, and it serves several games.
2. Validation: detect the container, check every file against a table, check the space, roll back
   on failure. We already do this (`game_setup/`); a launcher reuses it.
3. Gamepad navigation is the UI's own focus model fed by SDL's pad events, not a generic layer.
4. **No surveyed project has a real touch UI or an on-screen keyboard, and none ships for Android.**
   Steam Deck support is mostly documentation (Desktop Mode for the picker).
5. Translations: only Unleashed and Marathon; the N64Recomp tools are English only.
6. Launcher art is the projects' own (vector art, a resources submodule). None reads it from the
   user's files at run time. Ours has to work the same way: the base design is our own neutral art,
   and the user's images are an optional extra once the game is installed (section 5).

## 3. Technology options

| | Linux/Deck | Windows | macOS | Android | Gamepad | Touch | On-screen keyboard / IME | Translations | Size | License (GPL-3.0) |
|---|---|---|---|---|---|---|---|---|---|---|
| Dear ImGui | yes | yes | yes | SDL3 backend likely [not verified] | built in (`NavEnableGamepad` + SDL3 backend); we have `ui_gamepad.h` | as a mouse, `TouchPadding` | SDL3 backend (IME, `SDL_StartTextInput`) | our tables; CJK needs only a font since 1.92 | ~1 MB, already linked | MIT, fine |
| RmlUi | yes (Zelda64Recomp on the Deck) | yes | yes | [not verified] | RCSS `nav`, pad mapped to keys | native, inertial scrolling (6.2) | IME in its SDL backends (6.3) | `TranslateString` (our tables plug in), fallback fonts | a few MB + FreeType | MIT, fine |
| Qt Quick | yes | yes | yes | yes | no official Qt 6 module | yes | yes | full | tens of MB | LGPL/GPL, fine |
| Slint | yes | yes | yes | yes (C++ [not verified]) | not documented | yes | [not verified] | yes | a few MB + Rust toolchain | GPLv3 option, fine |
| CEF / system WebView | yes | yes | yes | CEF no; WebView per platform | none | yes | yes | full | CEF 100+ MB | CEF BSD, fine |
| Ultralight, Sciter, Noesis | — | — | — | no or [not verified] | — | — | — | — | — | proprietary: **not usable** |

Sources: ImGui <https://github.com/ocornut/imgui> (`docs/FAQ.md`, `backends/imgui_impl_sdl3.cpp`,
`backends/imgui_impl_android.cpp`: "Consider using SDL or GLFW backend on Android"); RmlUi
<https://github.com/mikke89/RmlUi>, navigation
<https://mikke89.github.io/RmlUiDoc/pages/rcss/user_interface.html>, input
<https://mikke89.github.io/RmlUiDoc/pages/cpp_manual/input.html>, localisation
<https://mikke89.github.io/RmlUiDoc/pages/localisation.html>; Qt licensing
<https://www.qt.io/licensing/open-source-lgpl-obligations>, QtGamepad in Qt 6
<https://lists.qt-project.org/pipermail/development/2025-May/046342.html>; Slint
<https://github.com/slint-ui/slint>; Ultralight <https://ultralig.ht/pricing/>; Noesis
<https://www.noesisengine.com/licensing.php>. Sizes are estimates **[not verified]**.

**Where the launcher draws.** Three ways:

- **Through our OGRE backend**: one GPU path, but `backend/` is a C API with no OGRE type outside
  it, the UI would sit inside the render module, and a launcher used to fix the render settings
  would fail with them (the GL3+ fallback case).
- **The SDK's ImGui drawer**: it lives in the runtime, which starts with the guest; the launcher
  runs before both.
- **Its own window with SDL3's 2D renderer** (`SDL_Renderer`: Direct3D, OpenGL/GLES, Metal, Vulkan
  depending on the platform), closed before the runtime opens the game's window: independent of
  OGRE and of the runtime, on every platform SDL supports. ImGui has an `SDL_Renderer` backend
  (`backends/imgui_impl_sdlrenderer3.cpp`); RmlUi has an SDL renderer backend too
  **[not verified for SDL3]**.

**SDL3 facts that shape it** (SDL 3.4.14 here):
- File pickers: `SDL_ShowOpenFileDialog` is asynchronous and must run on the main thread; on Linux
  it uses the XDG portal; on Android it returns `content://` URIs, to open with `SDL_IOFromFile`
  (<https://wiki.libsdl.org/SDL3/SDL_ShowOpenFileDialog>). So the install has to read the package
  through `SDL_IOStream`, not `std::filesystem`, to work on Android. The Steam Deck's Game Mode may
  have no picker at all (Unleashed's README): the launcher needs a file browser of its own as a
  fallback.
- On-screen keyboard: `SDL_StartTextInput` (<https://wiki.libsdl.org/SDL3/SDL_StartTextInput>); on
  the Deck SDL shows Steam's keyboard, and `SDL_HINT_ENABLE_STEAM_SCREEN_KEYBOARD` (SDL 3.4.12 and
  later) covers Big Picture on X11
  (<https://discourse.libsdl.org/t/sdl-added-sdl-hint-enable-steam-screen-keyboard/68554>).
- Android: an `SDLActivity`, the program built as a shared library, API 21 and later
  (<https://github.com/libsdl-org/SDL/blob/main/docs/README-android.md>). One window, one
  activity: the launcher and the game must share the process, which the in-process design does.

## 4. What the launcher has to cover

| Area | Today | In the launcher | Reuses |
|---|---|---|---|
| Game files | message boxes + system picker + progress window | a page: pick a package or a folder (system picker, our own browser as the fallback), check, extract with a progress bar, errors explained, reinstall | `game_setup/` (check, extract, table), `platform::PickFile` |
| Achievement set | a message box after the install | a choice with the explanation; editable later | `settings/host_settings`, `EnsureAchievementChoice`'s text |
| Video | the game's Video column | the same settings, before the game: no restart needed for any of them | `game_menu/video_menu_model` (the model, shared with the in-game column), `settings::Capabilities` |
| Language | the Video column; system language by default | a choice; also the launcher's own language | `settings`, `data/ui/*_strings.txt` |
| PC save import | the "load character" menu reads `import/` | pick the PC saves folder, copy into `import/` (never move), say what the game will do with them | `save_import/` (the plan), `docs/saves-research.md` |
| Mods | `mods/` + `mods.dat` by hand (branch `feature/pc-mods`) | list (name, author, description from `mod.dat`), enable/disable, order, "open the mods folder", import from PC's folder, the warning about saves and the backup | `src/mods/` (scan, `mods.dat`, the set digest, backup) |
| Play | — | start the game with these settings | the current startup path |

**Mods: what their side needs** (from `docs/mods.md` on `feature/pc-mods`; the mods agent could
not be reached on 2026-10-08, so these are open questions for it, not agreements):
- A small API for the launcher in `src/mods/`: list the mods with their `mod.dat` fields, read and
  write `mods.dat` (order and negative priority to disable), and the set digest that decides the
  backup. The launcher must not reimplement the format.
- When the backup runs: today at startup, before the guest, when the set changed. If the launcher
  edits the set, does it back up at once, or leave it to the start as now (recommended: as now, one
  rule)?
- The warning: the main menu's `CharacterModsWarning` has no text on Xbox. The launcher can say,
  before playing, which characters were saved with other mods (the save records their names,
  `docs/mods.md` 7d), once the save reading is shared.
- The "repair at load" condition of `docs/mods.md` 7d (saves written with the bug) has to be in
  place before mods reach players, launcher or not.
- Importing PC mods: copy the folders and PC's `mods.dat` from `%APPDATA%\Runic Games\Torchlight\mods\`
  (or a folder the player picks), keeping the priorities.

## 5. Restrictions

- **Nothing derived from the game in the repository**: no logo, background, font or sound of the
  game.
- **The base design uses only our own neutral art**: on the first start the launcher has no game
  files (it is there to install them), so it never depends on the game's images. Layout, colours
  and the project's name in a font we may ship; complete as it is.
- **The user's images, optional and after the install only**: `MarketplaceBanner.png`,
  `TitleIcon.png`, `DashboardIcon.png` in `game/`, or art read from `pak.zip` (miniz is already
  linked), always read at run time from the installed files; when one is missing or unreadable the
  launcher shows the base design. The same images feed the Steam shortcut (section 7).
- Fonts: one we may redistribute (license in `THIRD_PARTY_NOTICES.md`), with the Latin glyphs of
  de/fr/es; CJK only if those languages come.
- No environment variables (the project's rule); a command-line option to skip the launcher or to
  open it is fine.
- Translations through `data/ui` tables with a test that every text has every language, as
  `setup_text_test` does.
- Platform code (pickers, the mods folder opener, the Deck's keyboard) stays in the platform module.

## 6. Recommendation and stages

**Technology: Dear ImGui, in the game's process, before the runtime, in a window of its own drawn
with `SDL_Renderer`** (ImGui's SDL3 and SDL_Renderer backends). Reasons: it is what the process
already has (its gamepad mapping and its tests, the translations), it adds ~0 MB, its license is
MIT, it does not depend on OGRE or on the runtime (a launcher that must work when the render
settings do not), and every surveyed recomp with an installer runs it in-process before the game.
Its limits are looks (no CSS, no animation engine) and touch (a mouse with bigger hit areas):
acceptable for a first launcher, the only platform that needs real touch is Android, the last one.
**RmlUi stays the option if a console-style look becomes a goal** (MIT, proven in the same kind of
project with a gamepad); the launcher's logic (pages, models, the mods and install code) is kept
out of the drawing so that the drawing can be replaced.

**Stages** (each one a branch from `develop`, for the render agent to integrate):

1. **Shell and install**: the launcher window replaces the two message boxes and the progress
   window: install the package (or a folder), with progress, errors and the fallback file browser;
   the achievement set; Play. Shown when the game's files are missing, and on demand (a
   command-line option, and later a button in the game's menu that restarts into it). Keyboard,
   mouse and pad from the first version; texts in `data/ui` with their test.
2. **Options**: video and language before the game, sharing `video_menu_model` with the in-game
   column; shown at every start unless the player turns it off ("start the game directly"), as
   most launchers do.
3. **Mods**: once `feature/pc-mods` is in `develop` and its API is agreed: list, enable, order,
   import from PC, the saves warning.
4. **PC save import** from the launcher, on the existing plan.
5. **Steam shortcut with the user's art** (section 7): add the game to Steam as a non-Steam game
   with its grid, wide capsule, hero and icon made from the installed files, on Windows, Linux and
   the Deck; nothing written while Steam can overwrite it.
6. **Polish**: the user's own art inside the launcher, touch sizes, then a decision on RmlUi.
7. **Android**: the launcher is the smaller part (an SDL activity, `SDL_IOStream` for the package,
   a GLES path for the game). A project of its own.

**Stage 1 plan** (one branch, one commit per step; every step tested before the next):

0. **One ImGui, checked**: `third_party/imgui_backends/` gets `imgui_impl_sdl3` and
   `imgui_impl_sdlrenderer3` from ImGui's tag `v1.92.5` (MIT, in `THIRD_PARTY_NOTICES.md`); the
   core keeps coming only from `rex::runtime`. `launcher_imgui_test`: `IMGUI_VERSION_NUM` is 19250
   at compile time; `IMGUI_CHECKVERSION()` (the header's struct sizes against the compiled
   library's) and `ImGui::GetVersion()` equal the header's; no current context before, its own
   context while drawing, none after (the runtime's drawer creates its own later); one frame drawn
   with `SDL_CreateSoftwareRenderer` on a surface (no window, no display, so it runs in every CI
   job) and two pixels read back. A second ImGui would also fail the Windows link (LNK2005).
   The first run found SDL built without `SDL_Renderer` ("SDL not built with rendering support"):
   SDK patch 21 turns it on with the software driver only (approved 2026-10-08; branch
   `feature/launcher-imgui`).
1. **Model, no drawing**: `src/launcher/launcher_model` (pages: install, achievements, ready; what
   each button does; the install's progress and errors as `game_setup::Message`) and
   `file_browser_model` (the fallback browser: folders, `.` and `..`, files filtered by name,
   drives on Windows through the platform module). Plain C++ with unit tests, as
   `video_menu_model` is.
2. **Window**: in the platform module, an SDL window (1280x720, resizable; fullscreen when the
   desktop is gamescope) with the **software** `SDL_Renderer`, as the progress window today: no
   graphics library is loaded before the backend chooses the GPU. ImGui's SDL3 backends there;
   scale from `SDL_GetWindowDisplayScale`.
3. **Pages**: `src/launcher/launcher_view` draws the model with ImGui: install (package or folder
   with the system's picker, the fallback browser when it fails, the progress bar and cancel on
   the worker thread `first_run.cpp` already uses), the achievement set, Play and Quit. Keyboard and
   mouse from the SDL3 backend; the pad from ImGui's gamepad navigation (A activates, B goes back,
   as `live/ui_gamepad.h`).
4. **Texts and font**: the new texts in `data/ui/tl_setup_strings.txt` (de, fr, es) under
   `setup_text_test`. Font (decided 2026-10-08): Noto Sans, Latin subset only (OFL), in
   `THIRD_PARTY_NOTICES.md` (ImGui's built-in one is a small bitmap font that does not scale
   well). The install's progress is drawn with it too, so accented letters show as they are (the
   progress window's ASCII debug font drops the accents today).
5. **Wiring**: `game_setup::EnsureGameData`/`EnsureAchievementChoice` open the launcher instead of
   the message boxes when something is missing; Play closes the window, destroys the context and
   returns to `TorchlightApp::Create`, which goes on as now. `--launcher` (decided 2026-10-08) opens
   it on demand. The SDL message boxes stay only for an error before the launcher's window exists.
6. **Checks by hand**: Windows with an empty set of user folders (package, folder, cancel, a bad
   package, picker fallback); Windows Sandbox; Linux; the Steam Deck in Game Mode (the hand-over
   from the launcher's window to the game's).

**Done on `feature/launcher-imgui` (2026-10-08):** steps 0 (`9c2b39c`, SDK patch 21 `ab1319d`,
its macOS fix `5a98b6f`), 1 (`7e9547c`), 2 (`ddddad7`), 3 (`27ca6a9`, with `launcher_preview`, a
development executable), 4 (`42382c7`) and 5 (`92636a7`). Decided along the way: the check at
every start is every file of the table with its size plus `default.xex` by SHA-256
(`QuickCheckGameFolder`; it also checks `--game_data_root`, never installed into); after Play the
window waits for the keys and buttons held to be released; the colors are black and phosphor
green until the design review. Pending: step 6, the design review, the asynchronous pickers.

**Risks**:
- **ImGui twice in the process**: checked on 2026-10-08, there is one copy and the launcher must
  not add another. The SDK builds ImGui 1.92.5 (`thirdparty/imgui` at `6d910d5`, an OBJECT library
  without backends) into `rexruntime`: on Windows a DLL that exports every ImGui function and the
  `GImGui` pointer (seen in `rexruntime.lib`), on Linux a static library. The game already uses
  that copy (the achievement list, the runtime dialogs). The install has no backends, so the
  launcher brings only `imgui_impl_sdl3` and `imgui_impl_sdlrenderer3` from the same tag; a test
  checks versions and layout (section 6, stage 1 plan, step 0).
- **Two windows in a row**: the launcher's window closes before the runtime opens the game's;
  on the Deck in Game Mode and on Android the switch has to stay one visible window (Android: the
  same activity). To try on the Deck in stage 1.
- **File pickers**: the Deck's Game Mode may have none; the fallback browser is part of stage 1.
- **Pending (2026-10-08): the system's pickers block the launcher's frame loop.** Step 3 calls
  `platform::PickFile`/`PickFolder`, which wait (pumping events) until the picker closes: the
  launcher's window is not redrawn meanwhile, and some systems may show it as not responding.
  SDL3's dialogs are asynchronous (`SDL_ShowOpenFileDialog` with a callback, on the main thread):
  to evaluate in a later step a picker the launcher starts and then polls each frame (a pick in
  progress as a state of the model), so the window keeps drawing.
- **Restart into the launcher**: the runtime hard-exits on quit, so "back to the launcher" means
  starting the process again.
- **Mods are not in `develop`**: stage 3 depends on that branch and on the API above.
- **The first start and the menus are the render agent's area**: each stage is marked for their
  review.

## 7. Steam shortcut with the user's art

Goal: the launcher adds the game to Steam as a non-Steam game, with artwork made from the user's
installed files, so that it looks right in the desktop library, Big Picture and the Deck's Game
Mode. Everything local: the images are cut from `game/` at run time and written only into the
user's Steam folder; nothing of the game in the repository, nothing downloaded.

### How other tools do it

Steam ROM Manager (SRM), BoilR, decky-steamgriddb and NonSteamLaunchers all do the same two things:
an entry in `shortcuts.vdf` and image files in the user's `grid/` folder.

- **Where**: `<Steam>/userdata/<account id>/config/shortcuts.vdf` and
  `<Steam>/userdata/<account id>/config/grid/`; BoilR skips `userdata/0` and creates `config/`
  when missing (<https://github.com/PhilipK/BoilR/blob/main/crates/boilr-core/src/steam/utils.rs>).
- **Format**: binary VDF. `00 "shortcuts" 00`, then per entry `00 "<index>" 00` and its fields:
  strings `01 key 00 value 00`, 32-bit integers `02 key 00` + 4 bytes little endian, `tags` a nested
  map; `08` closes each map, the file ends `08 08`. Fields in Steam's order and casing: `appid`
  (int), `AppName`, `Exe`, `StartDir`, `icon`, `ShortcutPath`, `LaunchOptions`, `IsHidden`,
  `AllowDesktopConfig`, `AllowOverlay`, `openvr` (lower case), `Devkit`, `DevkitGameID`,
  `DevkitOverrideAppID`, `LastPlayTime`, `tags`
  (<https://github.com/PhilipK/steam_shortcuts_util/blob/main/src/shortcuts_writer.rs>). SRM
  writes `appname`/`exe` in lower case and reads keys case-insensitively, which a reader must too
  (<https://github.com/SteamGridDB/steam-rom-manager/blob/master/src/lib/vdf-manager.ts>).
- **The id**: `crc32(Exe + AppName) | 0x80000000`, written into `appid` as a signed 32-bit number;
  its unsigned value names the grid files; `(id << 32) | 0x02000000` is the 64-bit game id
  (SRM: <https://github.com/SteamGridDB/steam-rom-manager/blob/master/src/lib/helpers/steam/generate-app-id.ts>,
  BoilR: <https://github.com/PhilipK/steam_shortcuts_util/blob/main/src/app_id_generator.rs>). The
  stored `appid` is what counts: shortcuts Steam adds itself get other ids
  (<https://steamcommunity.com/discussions/forum/1/3361398061434096995>), and decky-steamgriddb
  looks shortcuts up by it (<https://github.com/SteamGridDB/decky-steamgriddb/blob/main/main.py>).
  `Exe` is quoted (`"..."`) and the quotes are part of the CRC
  (<https://github.com/SteamGridDB/steam-rom-manager/blob/master/src/lib/file-parser.ts>).
- **Images** (`grid/`, `<id>` the unsigned 32-bit id; names and the sizes SRM asks for,
  <https://github.com/SteamGridDB/steam-rom-manager/blob/master/src/lib/artwork-types/available-artwork-types.ts>;
  Valve's sizes, <https://partner.steamgames.com/doc/store/assets/libraryassets>):

  | File | Asset | Size | Where Steam shows it (Valve's page) |
  |---|---|---|---|
  | `<id>p.png` | library capsule ("grid") | 600x900 | library overview, collections |
  | `<id>.png` | library header ("wide capsule") | 920x430 | recent games and other places |
  | `<id>_hero.png` | hero | 1920x620 (Valve: 3840x1240, no text, a centred 860x380 safe area) | the game's page header |
  | `<id>_logo.png` | logo | up to 1280 wide or 720 tall, transparent | over the hero |
  | `<id>_icon.png` | icon | square (SRM: up to 600x600) | lists; the entry's `icon` field holds its absolute path |

  Which of them each view of Big Picture and Game Mode uses beyond Valve's list
  **[not verified]**; the plan writes four of them (capsule, wide, hero, icon) so every view has
  one. The logo's position is a `<id>.json` next to it, schema **[not verified]**;
  decky-steamgriddb sets one because shortcut logos show blank otherwise
  (<https://github.com/SteamGridDB/decky-steamgriddb/blob/main/src/hooks/useSGDB.tsx>).
- **Steam running**: Steam keeps the shortcuts in memory and writes `shortcuts.vdf` back, so the
  file tools close it first. BoilR runs `steam -shutdown`, waits up to 20 s, then signals, and
  starts Steam again
  (<https://github.com/PhilipK/BoilR/blob/main/crates/boilr-core/src/steam/restarter.rs>); SRM:
  `steam.exe -shutdown` on Windows, `kill -15` on Linux, restarted with `-silent` or through
  Flatpak
  (<https://github.com/SteamGridDB/steam-rom-manager/blob/master/src/lib/helpers/steam/stop-start-steam.ts>).
  In Game Mode Steam cannot be closed; Decky plugins call the client's own JavaScript API instead
  (`SteamClient.Apps.AddShortcut`, `SetShortcutIcon`, `SetCustomArtworkForApp(appId, base64, 'png',
  type)` with 0 capsule, 1 hero, 2 logo, 3 wide, 4 icon;
  <https://github.com/moraroy/NonSteamLaunchersDecky/blob/main/src/hooks/createShortcut.tsx>),
  which needs Decky Loader and is not something a game can do.
- **Finding Steam and the user**: Windows `%PROGRAMFILES(X86)%\Steam` by default (the registry's
  `HKCU\Software\Valve\Steam\SteamPath` **[not verified]** from a primary source); Linux
  `~/.steam/steam`, Flatpak `~/.var/app/com.valvesoftware.Steam/.steam/steam`; every numeric
  `userdata/*` but `0` (BoilR, link above). SRM reads `config/loginusers.vdf` and turns each
  SteamID64 into the account id by subtracting 76561197960265728
  (<https://github.com/SteamGridDB/steam-rom-manager/blob/master/src/lib/helpers/steam/get-available-logins.ts>).
- **Icon cache**: Steam keeps an old icon when it sees its cache written directly; decky-steamgriddb
  goes through a temporary file (main.py above).

### What the user's files give each image

From the installed `game/` only (section 1). The crops are generic (centred, by aspect ratio): no
coordinates tuned to a picture, so nothing about the art is in the code.

| Steam image | Source | Work |
|---|---|---|
| Capsule 600x900 | a 1024x725 art region from `pak.zip` (the loading screen or a class intro, DXT1) | decode DXT1 on the CPU, centre crop to 2:3 (483x725), scale 1.24x; `MarketplaceBanner.png` scaled into a band at the bottom, for the name |
| Wide 920x430 | the same art region | centre crop to 2.14:1 (1024x478), scale 0.9x; the banner over its lower part |
| Hero 1920x620 | the same art region | centre crop to 3.1:1 (1024x330), scale 1.875x (soft, acceptable for a background); no text, as Valve asks |
| Icon | `TitleIcon.png` (64x64) | as is, or scaled to 256x256 |
| Logo | none | the banner's logo sits on an opaque background and cannot be cut out generically; Steam shows the name instead |

The launcher shows these previews before writing anything, and the user can leave any of them out
(and replace them in Steam later, as with any game). The PNGs are written with SDL 3.4's
`SDL_SavePNG`.

### Design

- **Find**: Steam's folder per platform (Windows registry, else the default; Linux native and
  Flatpak) and the account from `loginusers.vdf` (asked when there is more than one).
- **Exe**: the installed executable's stable path (on Linux the packaged launcher script or
  AppImage, not a temporary mount), quoted; `StartDir` its folder; `AppName` "Torchlight Recomp";
  `AllowDesktopConfig` and `AllowOverlay` 1. With Flatpak Steam the game must be reachable from
  Steam's sandbox **[not verified]**: detected and explained, not worked around.
- **Steam closed**: before writing `shortcuts.vdf`, check whether Steam runs (in the platform
  module, from the process list). If it does, closing it is always the user's explicit choice,
  never automatic (decided 2026-10-08): a clear notice first ("Steam will close to add the game
  and open again"), then "close Steam and add" (`steam -shutdown`, wait, write, start it again) or
  cancel. Never write while it runs. Flatpak Steam and the views marked **[not verified]** above
  stay open until this stage starts.
- **Writing**: read the whole file, keep every entry and unknown field as read, replace ours (found
  by `Exe`) or append it, write to a temporary file and rename it; the first previous file is kept
  as `shortcuts.vdf.bak`. A unit test round-trips a file with other entries byte for byte.
- **Game Mode on the Deck**: Steam always runs. If the game was started from Steam (it already has
  a shortcut, found read-only in `shortcuts.vdf` by `Exe`), only the images are written to `grid/`
  under that shortcut's id, which does not touch `shortcuts.vdf`; whether Steam shows them without
  a restart **[not verified]**. Adding a new shortcut there means going to Desktop Mode, and the
  launcher says so.
- **Removing**: the same page removes our entry and our grid files (only files named with our id).

**Risks**: the binary format is undocumented (the tools above agree on it). A wrong write could
lose the user's other shortcuts, hence the backup, the temporary file and the round-trip test.
Steam's views and caches change without notice. Flatpak and Snap Steam sandboxes.
