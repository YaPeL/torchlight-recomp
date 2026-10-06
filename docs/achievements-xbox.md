# Xbox 360 achievements and a player choice between Xbox and PC sets

Research for offering the player one of two achievement sets: the game's own 12 Xbox 360
achievements, or the PC set (66 IDs, incomplete; see achievement-coverage.md). Reading only: the
guest code, the ReXGlue SDK (read only) and the user's game files at runtime. 2026-10-06.

## 1. How the Xbox achievements work in the recomp today

Guest side (details and addresses in achievements.md, "Existing Xbox boundary"):

- Seven gameplay sites call the award mapper `0x82375500` with one of the 36 event enums; a table
  maps 12 of them to Xbox IDs 1-12 and queues them (pending words `0x83558248`, dirty mask
  `0x835582DC`).
- The batch submitter `0x82193228` builds `{user_index, achievement_id}` entries and calls the
  XUserWriteAchievements-style wrapper `0x8287D910`, which sends `XMsgStartIORequest` to XAM app
  0xFB, message `0x000B0008`. Full-title and storage state gate the submission; with
  `license_mask = 1` (full game) the title gate is open. It has never been exercised in a run.

SDK side (`rexglue-sdk`, read only):

- `src/kernel/xam/apps/xgi_app.cpp` case `0x000B0008` reads the entries and calls
  `KernelState::UnlockAchievement(id)`, which calls
  `AchievementManager::UnlockAchievement(id, AchievementNotification::kShow)`.
- The manager keeps the unlocks and saves them to
  `<user_data_root>/achievements/58410A7E.toml` (`KernelState::LoadAchievementsData`,
  `SetUnlockSavePath`; title ID file name, `[unlocked.<id>] filetime = ...`). With the default
  user data root that is `~/.local/share/TorchlightRecomp/achievements/58410A7E.toml`, next to the
  PC profiles (`<profile>.state`); different files, no clash.
- On a new unlock it notifies listeners. `ReXApp::LaunchModule` registers its
  `AchievementToastDialog` (an ImGui toast with the XDBF icon) as a notification listener.

Per mode:

- Xenos (`--native_live=off`): the SDK's presenter drives its ImGui overlays, so the SDK toast
  shows; F7 opens the SDK's achievements overlay (`AchievementsOverlayDialog`, a host-side list).
- Only mode: the null GPU plugin has no presenter, so `ReXApp` creates no immediate drawer and no
  overlays (`rex_app.cpp`: neither branch of the presenter check runs). Unlocks are still saved
  to the toml, but nothing is shown: no toast, no F7 overlay. The native backend draws only the
  dialogs that `live/install.cpp` routes to its own `ImGuiDrawer` (XAM keyboard and message box).
- PC mode (`--pc_achievements=local|...`) replaces the mapper `0x82375500` and returns success
  before anything is queued, so no Xbox achievement is submitted or saved.

## 2. The "Achievements" button

- `mainmenu.uilayout` and `optionsmenu.uilayout` (pause) have an "Achievements" button with
  `onClick` `guiAchievementsMenu`. Their command handlers (`sub_8238CB98` @`0x8238CEF0`, main
  menu; `sub_82353BE8` @`0x82353DD0`, pause menu) call the wrapper `0x8287D8C0`, which imports
  `XamShowAchievementsUI` (`0x8302672C`).
- The SDK exports `XamShowAchievementsUI` as `REX_EXPORT_STUB` (`src/kernel/xam/xam_ui.cpp`): it
  logs "STUB" and returns. Nothing opens, in Xenos or in only mode, with or without the full
  license. In PC mode the app's override of `0x8287D8C0` returns 0, which is the same for the
  player.
- `trialachievementupgrade.uilayout` is the demo's upsell when an achievement would be earned; it
  is irrelevant with the full license.

## 3. Names, descriptions and icons of the 12 Xbox achievements

They are in the user's XEX, in the XDBF (SPA) resource. The SDK already reads them at runtime:
`KernelState::LoadAchievementsData` takes `title_xdbf()` and, for the console language
(`user_language`, falling back to an existing one), fills `AchievementInfo` with the label,
description, unachieved description, image ID, gamerscore and flags;
`AchievementManager::ListAchievements()` returns them, and `AchievementIconCache` decodes the
icon images from the same XDBF. Nothing is distributed: the app reads them through
`runtime()->kernel_state()->achievements()` while the game runs. The texts are the game's own,
localised for de/fr/es where the title has them.

## 4. Feasibility of "Achievements: Xbox 360 / PC (incomplete)"

### What each choice enables

| | Xbox 360 | PC (incomplete) |
|---|---|---|
| Guest Xbox path (mapper, queue, XUserWriteAchievements) | runs unchanged | suppressed (as today) |
| Native PC service, hooks, counters, flush points | off (`pc_achievements=off`) | on (`local`; Steam modes unchanged) |
| Saved progress | SDK toml `achievements/58410A7E.toml` | `achievements/<profile>.state` |
| In-game toast | CEGUI toast fed by the SDK's notification callback, texts from the XDBF | CEGUI toast as today, our texts |
| SDK ImGui toast | shows in Xenos; would duplicate ours, so ours is not used in Xenos (or the SDK's listener stays, see below) | not involved |
| "Achievements" button | list of the 12 with XDBF names, descriptions, icons, unlocked state | list of the 66 (60 reachable) with our texts, unlocked state and counter progress |

Both sets cannot be earned in the same session without a third mode; nothing here needs one.

### The list behind the button

The app already overrides the wrapper `0x8287D8C0` in PC mode; in both modes it would open our
list instead of the SDK stub. Two ways to draw it:

- ImGui modal: `live/install.cpp` already draws ImGui dialogs over the native backend in only
  mode (with gamepad navigation and the guest input cut while open), and in Xenos the SDK's drawer
  draws them. A dialog in `runtime()->imgui_drawer()` works in both modes. Icons: the Xbox ones from
  the SDK's icon cache; none for PC. Cost: low. Look: ImGui, not the game's.
- CEGUI, in the game's style like the toast: a scrolling list menu (the game's leaderboard or
  journal menus as layout models), a `CDropdownMenu` added to CGameUI like the video column, with
  the game's navigation. Cost: high (new menu, list widget, navigation hooks, all in the render
  front's `game_menu` area).

Recommendation: ImGui modal first (works everywhere, small), CEGUI later if the look matters.

### Where it is chosen, restart

- A host setting `achievements = "xbox" | "pc"` in `settings.toml` (host settings model), default
  `"xbox"` (complete set). It maps to `pc_achievements` at startup.
- Chosen in the first-start setup (`game_setup`, REL.4) with a one-line explanation, and changeable
  in the game's settings menu (a row in the video column, render front) with the existing
  "restart needed" notice.
- Restart required: the PC hooks read the mode once at `Install`, the SDK loads its unlock file at
  module launch, and switching mid-session would mix one session's events into two sets.

### Progress when switching

Each set keeps its own progress, untouched by the other: Xbox in the SDK toml, PC in the profile
state. Switching back shows the earlier progress. Nothing is converted between sets (achievements.md
already rules out importing the 12 Xbox IDs into PC state).

## Proposed design

1. Setting `achievements` (`xbox` default, `pc`) in the host settings, chosen in the first-start
   setup and in the settings menu, restart to apply; it selects `pc_achievements` (`off` or
   `local`). Steam modes keep their own flags.
2. Xbox mode: let the guest path run; register an app listener on the SDK's
   `RegisterNotificationCallback` that feeds our CEGUI toast (XDBF label in the player's language).
   In only mode this is the only toast; in Xenos either keep the SDK's ImGui toast and skip ours, or
   drop the SDK listener (`ReXApp::CreateAchievementNotificationDialog` is virtual and can return
   null) so both modes show the same CEGUI toast. Recommended: ours in both, for one look.
3. PC mode: as today.
4. "Achievements" button: override `0x8287D8C0` in both modes and open an ImGui modal list of the
   chosen set (Xbox: 12 with icons and gamerscore from the XDBF; PC: 66 with our texts, the
   unreachable and out-of-scope ones marked, counters with progress).
5. Validation: the Xbox path has never run to the SDK; the first run must confirm the submission
   (storage/profile gates), the toml, our toast and the list in only mode.

Open points to confirm before code: whether the settings row and the first-start page are done by
the render front (they live in `game_menu`/`game_setup`), and whether the Xbox toast should also
replace the SDK's in Xenos.

## Implemented: the set setting and the Xbox toast (2026-10-06)

- `settings.toml` key `achievements`, `"xbox"` or `"pc"` (`settings/host_settings.h`
  `AchievementSet`, `EffectiveAchievements`). Missing or invalid means `"xbox"`; an invalid value is
  reported like any bad setting and the app logs `achievements: no valid "achievements" setting;
  using "xbox"`. It needs a restart and is written back only when set (the file shows it as a
  comment otherwise), so it can be edited by hand.
- At startup `ApplyAchievementSet` turns the native PC mode on (`local`) for `"pc"`, unless
  `--pc_achievements` already chose a mode (Steam modes keep working); `"xbox"` leaves it off.
- Xbox set: `TorchlightApp::OnPreLaunchModule` registers `UseXboxAchievements` on the SDK's
  `RegisterNotificationCallback`; each new Xbox unlock goes to our queue as `XBOX:<id>` with the
  SDK's label, and the game-UI toast shows it. `CreateAchievementNotificationDialog` returns null,
  so the SDK's ImGui toast never exists (no SDK change).
- Run (only mode, full license, copy of the saves, no `achievements` key): sending the pet to town
  called `XGIUserWriteAchievements` (id 1), the SDK logged `Achievement unlocked: 00000001` and
  wrote `[unlocked.1]` to `<user_data_root>/achievements/58410A7E.toml`; our listener logged
  `Xbox achievement unlocked: 1 Fetch a Fair Price` and the toast showed once (`XBOX:1`; F9 capture:
  our title "Logro desbloqueado" over the official name). No PC unlock or toast. The real user data
  folder was not touched (listing before and after identical). Creating a character showed the XAM
  keyboard, an ImGui dialog drawn by our `UiOverlay`: ImGui works in only mode.
- The official names came in English with the console language set to Spanish: the SDK falls back
  to an existing XDBF language (`GetExistingLanguage`), so this title's XDBF seems to lack Spanish.
  The toast title stays in the player's language (our strings).

## For the render front: the settings row and the first-start page

- Key: `achievements` in `settings.toml`; values `"xbox"` and `"pc"`; default `"xbox"` (also when
  missing or invalid). Model: `HostSettings::achievements` (`std::optional<AchievementSet>`),
  `EffectiveAchievements`, `AchievementSetName`; `RequiresRestart(Setting::kAchievements)` is true.
- Takes effect after a restart (use the existing restart notice).
- UI texts (English; translations to de/fr/es on your side):
  - Row label: `Achievements`
  - Values: `Xbox 360` and `PC (incomplete)`
  - First-start explanation: `Choose which achievements to earn. Xbox 360: the game's original
    12 achievements. PC: the 66 achievements of the PC version; some cannot be earned yet. Each
    set keeps its own progress. You can change this later in the settings (restart required).`

## Implemented: the Achievements list (2026-10-06)

- The game's Achievements button (main and pause menus) no longer reaches the SDK stub: the app's
  override of `0x8287D8C0` opens an ImGui modal on the UI thread and returns success
  (`achievements/achievement_list.cpp`). Xbox set: the SDK's `AchievementsOverlayDialog` with a
  Close button (B/Escape too); icons come from the XEX through the SDK's immediate drawer in Xenos
  and, in only mode, through `live::DialogImmediateDrawer()` (the dialogs' `UiOverlay`, exposed for
  this). PC set: our list from `list_model.h` (unlocked state, counter progress, unavailable ones
  with the reason), no icons (the PC icons are Steam's).
- Validation (only mode; isolated `XDG_CONFIG_HOME`/`XDG_DATA_HOME` copies and a copy of the
  saves, the real folders unchanged): Xbox set from the main and pause menus showed the 12 with
  icons and "Fetch a Fair Price" unlocked, closed with B and with the mouse, the menu kept
  responding to the pad, the game stayed paused under the list; the PC set (`achievements =
  "pc"` in the copied settings) showed our list, without icons. No warnings in the log while the
  lists were open. Keyboard input after closing was not tried (nothing to type). The look is
  plain ImGui; a CEGUI version in the game's style is planned after the beta.
- That run found a bug, fixed: the set was read from `live::StartupHostSettings()`, which only mode
  fills in later, so `"pc"` was ignored; the app now loads `settings.toml` itself at startup.

## MAX_FAME parity (2026-10-06)

- PC completes MAX_FAME in its fame rank-up (`0x4A23B0`, `0x4A26BE`) when the rank equals the
  largest number of control points of the FAMEGATE graph (`0x524E10` -> `0x5CC440`). The Xbox
  rank-up (`sub_822A0570`) sends event 6 when the rank reaches the number of fame titles minus one
  (34 titles in `globals.dat`, so rank 33). The data are the same in both builds (FAMEGATE has 55
  points, the titles are 34), so the Xbox achievement fires 22 ranks before PC's.
- The rank can reach the graph's maximum: the fame gain `sub_822A0000` ranks up while the gate is
  reached and the rank is below `sub_82198E68` (the counterpart of `0x5CC440`); it never passes it.
- PC set: the guest's event 6 is ignored (`QualifiedGuestCompletion`), and a hook after the rank-up
  completes MAX_FAME when the rank == the maximum read from the guest's loaded FAMEGATE graph
  (`guest_abi::achievements::MaxFameRank`, no fixed number). The Xbox set is unchanged.
- Like PC, nothing else checks it: loading a save already at the maximum (for instance a PC save
  imported with the save import) does not unlock MAX_FAME, and no further rank-up can happen.
- In-game validation needs a character at the maximum. The guest's command executor has a `FAME`
  developer command, but the Xbox build has no way to type commands; development builds can run it
  with `--dev_guest_command` (below).

## Validation tool: `--dev_guest_command` (development builds only)

`--dev_guest_command="CMD;CMD;..."` runs commands of the guest's own developer command executor
(`sub_8234BE20`, the console object at CGameUI +5312: `FAME`, `DESCEND`, `ASCEND`, `DUNGEON`,
`LEVELUP`, ...). It is a validation tool, **not part of the releases**:

- The executing code is compiled only with the CMake option `TORCHLIGHT_DEV_COMMANDS` (off by
  default, `-DTORCHLIGHT_DEV_COMMANDS=ON`). The release workflow and the presets do not set it.
  Without it the flag is accepted and ignored with a warning in the log.
- `;` separates steps (the command line parser does not take a repeated string flag) and `&` the
  commands of a step (`src/dev/command_list.h`). One step runs after each game level load; its
  commands run in the same frame, for a command that loads nothing (`DUNGEON X` only sets the
  current dungeon, game +5132) followed by one that does (`DESCEND`).
- Each step runs from `CGameUI::Update` on the game's thread, once the HUD is up (context +5124 ==
  1) and no loading screen covers it. A command that loads a level (`DESCEND`) arms the next step.
  Each command and its result are logged as `dev: guest command`.
- `GOD` toggles the player's god flag (+1720, read by the virtual `sub_821E58B8`); while it is on,
  the health setter `sub_821D7E78` restores full health instead of lowering it. The flag is not
  saved. Context +5124 (PC +0x2428, the stats' "eligibility") is the game's state machine (1 =
  playing), written only by the game's own state code; no developer command writes it.
- `FAME n` adds `n` to the character's fame points (+948); it does not rank up by itself. Ranks
  follow at the next real fame gain (a champion kill, a quest) through `sub_822A0000`, which is
  what reaches the MAX_FAME hook. Developer commands also mark the character as a cheater (the
  executor sets +1721 and appends a suffix to its name), so use them on copies of the saves only.
- The cheater mark does not gate achievements in either build: PC reads its byte (+0x6D1 == 0xD6)
  only in the marker (`0x566530`), the console reply (`0x56837B`), two debug key handlers (through
  `0x40E550`), a copy into another object (`0x417765`) and save load/store (`0x4D9E37`..`0x4DA1BA`);
  the guest's readers of +1721 are the counterparts (`sub_8234A438`, `sub_8234BE20`,
  `sub_822965C8`, `sub_822807E0`, `sub_822D66A8`, `sub_8221CC70`). None is in the achievement code,
  so the PC set ignores it too.

### MAX_FAME validation run (2026-10-06)

PC set on copies of the config, data and saves, fresh state, development build with
`--dev_guest_command="FAME 2500000"` (above the FAMEGATE value of rank 55, 2,451,890). The command
ran once, at the HUD after the character's level load. At the next real fame gain the guest ranked
up from 1 to 55 in one call to `sub_822A0000` (54 rank-ups in the same millisecond, through 33
without stopping); ranks 2..54 unlocked nothing, rank 55 unlocked MAX_FAME once and its toast
showed. The rank stayed at 55 (no later rank-up). No error or warning in the log about the titles
above 33. The only other unlock was TORCHLIGHT_ACHIEVEMENT_FIRSTLEVEL, from the guest's own retained
completion path during play (the state was fresh), not from the command. The real user folders
were unchanged. Afterwards the build went back to `TORCHLIGHT_DEV_COMMANDS=OFF` and the executable
has no command execution code.

## REACH_LVL_50/100 parity and validation (2026-10-06)

- Predicate, PC: right after the level load `0x415820`, both callers (`0x4190EF` game state
  update, `0x41A232` floor transition) compare the current level's depth (game +0x38, level +0x140)
  with STAT_DEEPEST_FLOOR and raise it through `0x5F7130`, which only writes while the game state
  +0x2428 is 1 (playing); the achievements are evaluated at the next flush point. The stat is the
  deepest depth ever reached, not the current floor. Ours: the same maximum after `sub_82217DA8`
  from its three game callers, gated on the guest's state (+5124 == 1), evaluated at the next flush.
- The field is the same: both level constructors store their depth argument twice in a row
  (PC `0x4F91C1` +0x140/+0x144, guest `sub_822E8E30` +296/+300). Depth is zero-based (floor 50 =
  depth 49).
- Target depth, the same in both transitions (PC `0x419A51`, guest `sub_82214818` @0x82214B00): an
  explicit target if not 0; otherwise 0 when the target is the dungeon named by a global string
  (guest `0x8306DF58`, PC `0xBF8674`); otherwise current depth + delta. The town's dungeon entrance
  requests delta 0 and target 0 from the town (depth 0), so every dungeon counts from 0: the Shadow
  Vault (`RANDOMDUNGEON`) does not add the main dungeon's depth, in either build.
- `DESCEND N` requests the current dungeon (game +5132) at depth current + N as an explicit target
  (from the town: the main dungeon at N - 1) through the same request `sub_82214640` the stair
  triggers use, then the same transition and load (return `0x82215058`). The predicate reads
  nothing else (no visited floor), so it exercises the same path; what it skips is the stair
  trigger itself, validated by playing on 2026-10-05.
- Run (PC set, copies, fresh state, new non-hardcore character, `GOD` on): first launch
  `GOD & DUNGEON RANDOMDUNGEON & DESCEND` from the town reached depth 1 in the Shadow Vault (the
  save names `RANDOMDUNGEON`; the floor's theme comes from the vault's random pool). Second launch
  `GOD & DUNGEON RANDOMDUNGEON & DESCEND 47 ; DESCEND ; DESCEND 49 ; DESCEND ; DESCEND ; ASCEND`
  from depth 1: depths 48, 49, 98, 99, 100, 99, all with state 1. Depth 48 and 49 unlocked nothing
  on arrival; REACH_LVL_50 unlocked at the flush that starts the next load (to 98) and its toast
  showed after it; REACH_LVL_100 likewise at the flush after depth 99. No other achievement
  unlocked, each once; the real user folders were unchanged; the build went back to
  `TORCHLIGHT_DEV_COMMANDS=OFF` afterwards.

## For the integrator (2026-10-06)

Branch `feature/pc-achievements` (pushed), 57 commits ahead of `origin/main` at `ec3cc70`.

- Trial merge into `origin/main` (a scratch worktree, not committed): no conflicts (`CMakeLists.txt`
  merges automatically; `LICENSE`, `patches/LICENSE` and the README's License section are already
  identical on main). The merged tree builds (RelWithDebInfo, full codegen) and passes all 48
  tests.
- `f1aa068` (`live: expose the dialogs' UiOverlay as their immediate drawer`) is the only change
  in `src/live/`: `live::DialogImmediateDrawer()` returns the only-mode dialogs' `UiOverlay` so the
  Xbox achievement list can draw its icons. One function, no behavior change otherwise.
- Other shared files touched: `src/settings/host_settings.{h,cpp}` (the `achievements` key,
  restart-only) and `src/torchlight_app.h` (installing the achievements runtime, the set, the Xbox
  toast and the list button).
- Not in the code yet, for the render front: the settings row and the first-start page with the
  texts above ("For the render front"); until then the key is set by editing `settings.toml`
  (default `"xbox"`).
- Data: `data/ui/tl_achievement_strings.txt` (the toast and list texts in de/fr/es) is copied next
  to the executable by `torchlight_data` and installed with `data/ui`, so packages carry it
  without changes.
- User data: the PC set's state is `<data folder>/achievements/<profile>.state`; nothing to migrate
  for new installs. The Xbox set's unlocks stay where the SDK keeps them.
- Release builds: `TORCHLIGHT_DEV_COMMANDS` stays off (no workflow or preset sets it); the
  `--dev_guest_command` flag is then ignored with a warning.
- No SDK patch is needed by this branch. After the merge, the branch-only reminder to relink by
  hand after an SDK reinstall no longer applies (main copies the SDK's plugins).

## Differences from PC and why (2026-10-06)

The PC set follows PC's code. Where the Xbox build lacks PC's interface or keeps a different one,
these are the adaptations, each from the code of both builds:

- Levers (PULLED_LEVERS_100): not an adaptation but worth knowing: PC counts every trigger unit with
  no SPAWNCLASS and MAXSTATES 2, which includes doors, stairs and portals, not only levers; the Xbox
  set does the same (achievements.md, stat producer inventory).
- Pet potions (PET_POTIONS_50): no adaptation. PC counts a potion used on the pet (its use
  path); on Xbox the potion is moved to the pet's inventory (X) and used there (A), which reaches
  the same use path (2026-10-06 run). The earlier "not available on Xbox" was wrong: that potion
  had only been transferred.
- Teaching the pet a spell (PET_TRAINER): PC teaches by clicking one of the pet panel's two spell
  slots with a spell scroll on the cursor (0x58F530) and sends the achievement right after using the
  scroll on the pet, whatever the use does. The Xbox build has no such slots: a pet learns by using
  the scroll from its own inventory (0x8236BB70), with a replace dialog when both slots are taken.
  It is the same action in the console's interface, so the Xbox set counts it at the same relative
  point, the scroll's use on the pet (return 0x8236BC3C, also reached after the replace dialog picks
  a slot; a cancelled dialog uses nothing and does not count), with PC's conditions: a spell, a pet,
  subtype not 41/42. Unlike the potions there is an equivalent action, so it is accepted.
- The pet's full bag (BEAST_OF_BURDEN): no adaptation. PC checks the state in the player's
  per-frame update; the guest's counterpart lost the check, and the Xbox set restores it in the same
  function with the same rule (category 0 items == its capacity, game playing).

### Pet runs (2026-10-06)

PC set on copies (`out/validation/pet`), development build; items created with the executor's
name fallback (a text that is not a command spawns the unit of that name next to the player).

- PET_TRAINER: a spell scroll moved to the pet's inventory (X) and used there (A) counted once and
  unlocked PET_TRAINER with its toast. In the next launch three more uses counted (second spell,
  then the replace dialog: cancelled, which did not count, then a slot chosen).
- Pet potions: with the pet hurt (`HURTME 10`), a health potion moved to its inventory and used
  there healed it and counted one pet potion. PET_POTIONS_50 is therefore reachable on Xbox, unlike
  what the 2026-10-05 notes concluded (that potion was only transferred, never used).
- BEAST_OF_BURDEN: the first check never matched a full bag. The agreed diagnostic showed the hook
  running, the pet and its inventory found, but the category lookup failing: the pet's inventory
  lists one category (id 5), not id 0, and PC falls back to the first category when the id is not
  listed (0x4E59D0). With that fallback the full bag (50 of 50) unlocked BEAST_OF_BURDEN with its
  toast. The diagnostic was removed afterwards.

### Quit-to-menu hang after teaching the pet a spell (2026-10-06, fixed in 42d1c8f)

Symptom: after a spell scroll was used from the pet's inventory, quitting to the main menu from the
same level hung in an endless runtime loop of "Unhandled guest access violation: read of guest
0x00000038" (once 0x00000027) on the game's thread, right after the level teardown. Runs of that
scenario (new character, scroll taught, quit to the menu):

| Configuration | Hangs |
| --- | --- |
| PC set | 3 of 3 |
| PC set under gdb | 0 of 2 (timing-dependent) |
| Xbox set (no PC hooks active) | 0 of 3 |
| PC set, pet bag check disabled (local test) | 1 of 2 |
| PC set, PET_TRAINER type query removed (local test) | 0 of 2 |
| PC set after the fix | 0 of 3 |

Cause: the PET_TRAINER hook called the guest's unit type test (0x821D7CE0) with a copy of the
context from inside the item use. Reading the guest functions did not show what breaks; the runs
do. The fix walks the same type tree from the host (guest_abi `UnitIsType`) and the hooks no longer
call guest code there; every link the per-frame and item-use hooks follow must also point above the
lowest 64 KiB (a host read of an unmapped guest page is a fault the runtime cannot resolve). Two
agreed diagnostics used on the way were removed. The GPU wait hook (main) was checked and is not
involved. The pet-potion producer's `__RTDynamicCast` call (the same pattern) was replaced the same way:
guest_abi `CastToCharacter` reads the MSVC RTTI from the host (the guest image has a single
CCharacter base class descriptor, 0x82118BFC, non-virtual at displacement 0, shared by every derived
class).

The toast under the replace dialog was checked again (2026-10-06, a pet with both slots taken,
the third spell taught through the dialog): it showed. The earlier unseen toast was not a toast
problem; plan B (hold the toast while a menu covers it) is not needed for now.

Validation of the host cast and type test (2026-10-06, development build): a potion used from the
hurt pet's inventory counted (`pet-potion`, the cast read from the host); the player's hits in the
town and in the first mine floor recorded applied damage (the attacker's type 28 read from the
host); quitting to the main menu from the mine was clean. Note for validation runs: `HURTME n`
subtracts n% of the health, it does not set it to n%.
