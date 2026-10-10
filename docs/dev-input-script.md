# Input scripts: runs with no one at the controller

Development builds can play a script of controller input into the game
(`--dev_input_script=FILE`), so recording, profiling and validation runs need no one at the
controller. Any agent can repeat the same sequence on Linux, Windows or macOS.

## Building and running

- Configure with `-DTORCHLIGHT_DEV_INPUT=ON`, and also `-DTORCHLIGHT_DEV_COMMANDS=ON` for scripts
  that use `command` (the standard measurement does). Both are off by default and never set by the
  release workflows. A build without them logs that `--dev_input_script` is ignored.
- Run the game with `--dev_input_script=PATH/TO/script.txt`, through
  `tools/run_capped/run_capped.py` (time and log caps, nothing left behind):

```
python3 tools/run_capped/run_capped.py --timeout 300 --watch <the game's log folder> \
  --output run.out -- <torchlight executable> --game_data_root <game data> \
  --user_data_root <a COPY of the user data> --capture_dir <captures> \
  --dev_input_script tools/input_scripts/load-and-quit.txt
```

- Always on a copy of the user data (`--user_data_root`): scripts save the game, and
  `new-character` adds a character.
- The game window still opens on the screen. Running the game still needs the owner's OK, as any
  run does.

## How it works

- **The controller.** The script is a controller of its own: a synthetic input device that the
  SDK feeds to guest user 0 together with any real controller (`dev/script_input.cpp`). It needs
  no SDL, no window focus and no SDK change. A real controller still works during the run, so a
  person can take over.
- **Timing.** The script advances once per guest frame (the swap), so durations are in guest
  frames or seconds.
- **Waits.** They are for game events, never only for time:
  - `level_loaded`: the game's level load completed;
  - `menu NAME`: a menu whose class name contains `NAME` opened. Menus are named by their MSVC RTTI
    class through the base `CDropdownMenu::SetOpen`. Every menu that opens is logged as
    `dev input script: menu opened: .?AVName@@`.
  - Each wait has a timeout (120 s by default). When it passes, the script fails with the line in
    the log (`dev input script FAILED: line N: ...`) and closes the game.
- **Marks.** A `mark` logs `perf step N: TEXT` when it starts. When the next mark starts (or at the
  end), it logs that step's frame summaries: game and presented FPS, p99, max and dropped frames,
  in the same lines the step overlay wrote. The measurement and thermal scripts read these.
- **`capture`** requests an F9 capture of the next frame. **`command`** runs a guest developer
  command (needs `TORCHLIGHT_DEV_COMMANDS`). **`quit`** closes the game window as the player would.
- **What stays random.** The game's own randomness (monsters, drops) is not fixed by the script.
  Compare several runs, not one, or use the deterministic time below.
- **Deterministic time** (`--dev_deterministic_time`, with a script): the C runtime's `time()`
  returns a fixed date, so `rand()`, which the game seeds with it, gives the same numbers, and the
  game's counter (`QueryPerformanceCounter`, its frequency, `GetTickCount`) moves 1/60 s per guest
  frame instead of with real time (`dev/deterministic_time.h`; the functions and their evidence in
  `guest_abi/xapi_time.h`). The game then runs at its frame rate divided by 60 times real speed,
  and the script's seconds are these virtual ones. Threads that load in the background still finish
  when they finish, in real time.

## The script

One command per line; `#` starts a comment. Durations are `N` or `Ns` (seconds), `Nms`, or `Nf`
(guest frames). Buttons: `A B X Y START BACK LB RB LT RT UP DOWN LEFT RIGHT LS RS`.

| Command | What it does |
|---|---|
| `press BUTTON... [frames N]` | down N frames (default 6), then up as long |
| `press BUTTON... every DUR until EVENT [timeout DUR]` | press again every DUR until the event |
| `press BUTTON... every DUR for DUR` | press again every DUR, for that long |
| `hold BUTTON... for DUR` | hold |
| `stick left\|right X Y for DUR` | a stick at X, Y from -1 to 1 (Y up is positive) |
| `idle DUR` | nothing pressed |
| `wait level_loaded \| menu NAME [timeout DUR]` | wait for the event |
| `mark TEXT` | a step of the run |
| `capture` | an F9 capture |
| `command TEXT` | a guest developer command, e.g. `ASCEND` (to the town from the mine's first floor) |
| `quit` | close the game |

An event only counts if it happens after its command starts.

## The game, as the scripts see it

- **Title screen.** It wants a button press. `press A every 3s until menu MainMenu` gets past it
  whatever its timing.
- **Main menu (`CMainMenu`).** Its entries, top to bottom: Continue, New Character, Load Character,
  Achievements, Leaderboards, Help and Controls, Options, and Back to Library at the bottom.
  - Continue loads the last character played: no menu opens, the next event is a level load.
  - New Character (one DOWN, then A) opens `CNewGameMenu`.
- **New character (`CNewGameMenu`).** The focus starts on the class row. Below it: player name,
  pet, pet name, and OK, four steps down. A on OK opens `CDifficultyMenu`, and A there (the default
  difficulty) loads the town. The town greets a new character with a tip that B closes.
- **The fixed-floor saved game** used by the measurements. The character stands by the stairs up
  of the mine's first floor; A there takes the stairs. West over a bridge there are spiders.
  - `ASCEND` (developer command) takes it to the town. It always lands at the same place by the
    town's east bridge, next to the vendors.

## Example scripts (`tools/input_scripts/`)

| Script | For | Needs |
|---|---|---|
| `load-and-quit.txt` | load the saved character, stand, capture, quit | |
| `new-character.txt` | create a character with the defaults and stand in the town | a copy of the user data |
| `measure-fight-town.txt` | the standard measurement on the fixed-floor saved game: mine floor 1 standing still and fighting, the town square walked around, steps marked | `TORCHLIGHT_DEV_COMMANDS` (for `ASCEND`) |

Copy one and change it rather than starting from nothing. A new menu's class name shows in the
log as soon as it opens. Captures, rendered with `tools/replay`, show where the character stands.
