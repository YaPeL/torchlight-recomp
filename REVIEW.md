# Reviewing changes

How a review is done in this repository, for people and agents. The project's rules are in
`AGENTS.md`; this file says what counts as a failure and how findings are reported.

## What a failure looks like here

- A change that reads, writes or moves the owner's real saves or real user folders (data, config,
  logs, caches) outside a test profile.
- A new environment variable (`AGENTS.md`: settings or a command-line option instead).
- Game files in git: assets, executables, extracted data, captures, saves, run logs, or text copied
  from the game or its translations.
- Direct3D 11 and GL3+ behaving differently: anything the render system keeps or resets on its own
  (state, bindings, constants) that only one of them does. Example: 0338b58, constants lost after a
  target bind on Direct3D 11 only.
- A changed default or anything a player sees, without the owner's OK.
- A slower start: work added before the backend draws (`startup: <step> in N ms` in the log).
- Broken isolation under `--user_data_root`: a file of the player's read or written by a test run.
- A difference classed as ours or as "the game's own behaviour" without evidence: the same frame
  against Xenos (`--native_live=off`, parallel mode or an F9 reference), or the guest code that does
  it, read and cited.

## The reviewer's rules

- Every finding names `file:line` and the case that fails (inputs or state -> wrong result).
- Every finding is marked **Important** (must be fixed or justified before the merge), **Nit**
  (worth doing, does not block) or **Pre-existing** (not introduced by the change; noted for later).
- At most three nits. No findings about style.
- The summary starts with the number of findings, and says so plainly when there are none
  ("0 findings").
- A finding that comes up a second time goes into `AGENTS.md`, a test or a lint, not into another
  review comment.

## Sensitive areas

These always get a review in a clean context (a reviewer that has not written the change), and
the owner sees the evidence (test output, run logs, measurements) before the merge:

- the repair and checks of saves (`src/mods/save_*`, `src/game_menu/save_*`);
- the user's data paths (`src/platform/user_folders*`, `src/settings/user_folders_in_use*`,
  anything that resolves where files are read or written);
- packaging and release (`packaging/`, `tools/release/`, `.github/workflows/release.yml`);
- CI workflows (`.github/workflows/`);
- the SDK's patches (`patches/`).

Documentation-only changes are integrated without a review in a clean context once CI is green.
