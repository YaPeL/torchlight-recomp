# Select Character auto-selection: bounded BTN_SOUTH observation

User clarification: the unwanted selection immediately starts loading gameplay,
not merely returning to the main menu with a character focused.

Scope: physical A/confirm is evdev `EV_KEY BTN_SOUTH=304`. No debounce,
suppression, input injection, guest edits or renderer changes are authorized.

## Observer

`TORCHLIGHT_SOUTH_OBSERVER` is a separate Linux CMake option, default OFF.
`tools/south_select_observer.cpp` is inactive unless `TORCHLIGHT_SOUTH_DIR` is set.
`TORCHLIGHT_SOUTH_DEVICE` names the controller's separate read-only evdev fd;
it never grabs the device. The fd requests CLOCK_MONOTONIC timestamps, and
startup logs a monotonic/realtime clock pair. SDL source timestamps are retained
separately; their epoch is not assumed to match evdev.

Touch `DIR/arm` while the main menu is ready. The next physical BTN_SOUTH down
selects one window, from 250 ms before down to 1 second after its first up.
All raw events (including duplicates and repeats) remain visible. No release
within 10 seconds produces a clearly labeled safety-cap capture. A 32768-record
cap bounds memory and reports any overflow. No continuous disk trace is made.

Three weak observation sites in the SDL driver record each drained update,
translated button event (including the pre-update mask), and returned device
state. Original input operations and locking order are preserved. Patch:
`patches/rexglue-south-observation.patch`. All 16 bytes of X_INPUT_STATE are saved.

Host forwarding hooks call the original XamInputGetState and
XamInputGetKeystrokeEx unchanged, then save the result, output bytes and guest
caller LR. Failed polls are marked and their output is not treated as valid.
The guest update routine 0x821FE8A8 gets a separate sequence counter. The existing
overlay guest-submission and host-present atomics are read using addresses
resolved from the exact executable symbol table; no FPS hook is modified.

## UI markers established from existing guest image / generated disassembly

- 0x82386D28 loads `media/ui/characterload.uilayout`.
- 0x823874D8 activates/deactivates this screen using the original r4 boolean.
  It calls the base visibility routine 0x82351FF0, refreshes the list and invokes
  0x82389D30 with index 0 / force=1 during activation. Initial focus alone is
  **not** acceptance.
- 0x82389D30 manages selected index at object+272 and scroll offset at +268.
- 0x8238A598 dispatches this screen's numbered GUI actions. Action 14 on an
  already-selected first row writes owner+6024=2 and owner+6028=0, then hides
  the screen. The layout binds `guiSelect1` to its first row. This is the
  candidate acceptance path to correlate, not evidence that it actually runs.
- 0x8238A2E0 handles this screen's CEGUI events; event+28 and widget+308 action
  are recorded along with entry/exit state. Next/previous focus helpers are
  observed too.

All wrappers forward the untouched PPC context to their original `__imp__`
functions, then record state. UI snapshots include the original arguments,
selected/scroll indices, visibility bytes, owner result fields, window pointer
and a bounded read-only guest return-chain sample. Failed diagnostic reads
return zero rather than faulting the game.

Artifacts live in `docs/bringup-artifacts/south-select/`. `capture.jsonl` retains
the raw fixed-format records; `tools/analyze_south_select.py DIR` decodes exact
state bytes and derives A edges **separately** for each source/user/device.
`guest-input-ui-disassembly.txt` retains the relevant original PPC instructions.

## Status

SDK Release runtime and RelWithDebInfo application observer builds passed.
The NVIDIA run is open under GDB. Runtime reports the actual GTX1050Ti Max-Q;
observer opened event10 with initial A=up. Waiting for the user to highlight the
menu item before arming. No reproduction or input-category conclusion yet.
The previously fixed Vulkan plugin is unchanged. Physical controller interaction
is required after launch; the observer is armed separately from startup/menu input.
