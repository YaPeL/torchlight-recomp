# perf/shader-cache on Direct3D 11: the program cache and the precompilation

Windows, 2026-10-17 and 18. RTX 5080, 3840x2160, D3D11. Release builds from scratch (SDK
D:\rexglue-sdk-33). Isolated profile (`--user_data_root`, a copy of the plain test save, no mods).
The route each session is start, Continue, town, into the mine, one floor, quit from the menu.

The cache folder is still `%LOCALAPPDATA%\TorchlightRecomp\ogre`, because `--user_data_root` does
not move the cache kind (noted as pending isolation work). Before the first session of each build,
only `microcode-*.bin` and `programs.txt` were deleted there; the RTSS's `.hlsl`/`.glsl` sources
stayed. `session-N-cache.txt` records the files before and after each session (size, write time,
SHA-256 prefix). The rest is as in `README.md`: `analyze.py` output, log excerpts. `analyze.py`
now also lists the precompile lines, the OGRE log's program cache lines and the title screen's
frame with the most program time (the reported frames in the first 15 s).

## 362390b (the cache saved only when it grew; a cut file detected), ctest 105/105

Run on 2026-10-18. All three sessions start from the same save (copied before session 1 and
restored before sessions 2 and 3). `watch.ps1` ran during each session and recorded, once a second,
the size and write time of `microcode-*.bin` and `programs.txt`. Its output is
`session-N-bin-watch.csv`; a row is marked "rewritten" when the .bin's write time changed.

| | Session 1 (empty cache) | Session 2 | Session 3 (.bin cut in half) |
|---|---|---|---|
| Level loads (ms) | 5073, 3032, 4527 | 4960, 2712, 4456 | none (title screen and quit) |
| Precompile lines | none (empty list) | 44 in 576 ms "(more at the next load)" (start); 123 in 801 ms (first load) | 44 in 549 ms "(more at the next load)" (start) |
| OGRE log on the cache | no file yet | "program cache: <path>" | "program cache unreadable, rebuilt: <path>" |
| .bin rewrites (watch.ps1) | 3, one after each load: 269082, 379567, 428625 B | 1, after the third load: 428625 -> 518465 B | 1, at exit: 259232 -> 177991 B |
| Distinct programs (OGRE log) | 154 | 184 (154 of session 1 + 30 new) | 68 |
| Reported slow frames with programs | 8 (20 generations, median 26.34 ms) | 0 | 0 |
| Title screen frame with the most program time | 10 programs / 263.4 ms | none | none |

Session 2:
- The .bin was not rewritten after the first or the second load. It was rewritten once, at
  14:39:39.798, right after the third load ended (14:39:39.674). The 30 programs that session 1
  never created all appear between 14:39:38 and 14:39:39, during that load (`tl_rtss#168` to
  `#194`: 25 fragment and 5 vertex programs). The floor was different from session 1's, even with
  the same save. They are listed in `session-2-ogre-log.txt`, and `programs.txt` gained their keys
  in the same rewrite.
- No reported slow frame generated a program, after the loads or on the title screen.
- The precompile still splits in two: 44 programs at the start "(more at the next load)", then
  the remaining 123 during the first load.

Session 3 (the .bin cut from 518465 to 259232 bytes; title screen, then quit):
- The game started normally (0 [error] lines apart from SAVE:, clean exit).
- The cut file is now detected: OGRE's log says "program cache unreadable, rebuilt: <path>".
  The earlier "program cache: <path>" line is gone.
- At exit the .bin was rewritten as 177991 bytes. That is a whole file: its trailer is `tlpcach1`,
  the payload length matches the file and the FNV-1a checksum matches (checked after the session).
  But it holds only what this session compiled: the 44 programs precompiled at the start, plus the
  title screen's. The rest of the list was not reached, because no level was loaded. So it is
  smaller than session 2's 518465 bytes; the next session's precompile compiles the rest.

## 261bc31 (precompilation through the whole list), ctest 103/103

Both sessions start from the same save (copied before session 1, restored before session 2).

| | Session 1 (empty cache) | Session 2 | Session 3 (.bin truncated) |
|---|---|---|---|
| Level loads (ms) | 5090, 3148, 5056 | 4962, 2750, 4427 | 5001, 3071 |
| Precompile lines | none (empty list) | 46 in 555 ms "(more at the next load)" (start), 150 in 1046 ms (first load) | 42 in 520 ms "(more...)" (start), 157 in 1176 ms (first load) |
| OGRE "program cache:" | no file yet | the .bin | the .bin (see below) |
| Distinct programs (OGRE log) | 187 | 191 (187 of session 1 + 4 new) | 191 |
| Reported slow frames with programs | 5 (17 generations, median 11.15 ms) | 0 | 0 |
| Title screen frame with the most program time | 10 programs / 155.0 ms | none | none |
| .bin after | 529628 B, 89E9A946... | 544380 B, 7CBE3026... (rewritten) | 544380 B, 7CBE3026... (rewritten) |
| programs.txt after | 166619 B | 169169 B (4 new keys) | unchanged |

Session 2:
- No program was generated in a reported slow frame. The burst of 49bbb88 (9 frames of about
  27 ms with 6 programs each, right after the first load) is gone. The title screen has no
  reported frame with programs.
- The precompilation still does not finish before the title screen. At the start, 46 programs
  went through in 555 ms "(more at the next load)". The rest, 150 in 1046 ms, went during the
  first load, with nothing left after it.
- The .bin is not identical at the end: it grew from 529628 to 544380 bytes. The 187 programs of
  session 1 all came from the cache. The 4 new ones were compiled and added: two at 23:53:29 and
  two at 23:55:23, at the end of the second and third loads. They are programs that session 1 never
  drew, even with the same save and route (different monsters or effects). `programs.txt` gained
  their keys too. So session 2 compiled nothing that was already cached.

Session 3 (step 4): the .bin was truncated from 544380 to 272190 bytes, and the session went past
the title into two loads.
- The game started and ran normally (0 [error] lines apart from SAVE:, clean exit).
- The log does NOT say "program cache unreadable, removed". OGRE's `loadMicrocodeCache` catches
  the exception itself: `OgreGpuProgramManager.cpp:376-391` (the warning at :389) logs "Could not load Microcode Cache:
  Chunk length field exceeds remaining stream data", clears the cache and returns. So
  `tl_backend_open_program_cache`'s catch never runs, the file is not removed, and the next line
  says "program cache: <path>" as if it had loaded.
- The recovery works anyway. The cache was empty, so the precompilation compiled everything again
  (42 + 157 programs, in about the same time as session 2), and the exit rewrote the .bin whole:
  544380 bytes, the same SHA-256 as at the end of session 2. No reported frame had programs.
- A version mismatch (`chunk->version != 3`, the warning at `:396`) takes the same path: a warning
  ("Invalid Microcode Cache") and return, no exception.

## 49bbb88 (first version), ctest 103/103

Session 2 continued from where session 1 left the save, so the routes differ (3 loads, then 2).

| | Session 1 (empty cache) | Session 2 |
|---|---|---|
| Level loads (ms) | 5069, 3121, 3980 | 5022, 3400 |
| Precompile lines | none (empty list) | 14 in 187 ms, 29 in 291 ms, 18 in 164 ms, all "(more at the next load)": 61 of 195 |
| Distinct programs (OGRE log) | 184 | 151 |
| Reported slow frames with programs | 16 (41 generations, median 10.53 ms) | 11 (60 generations, median 4.46 ms) |
| Title screen frame with the most program time | 10 programs / 146.5 ms | none |
| .bin after | 518441 B, 5D84582D... | unchanged (same SHA-256 and write time) |

In session 2, 9 consecutive frames at 23:31:08.5-09.0 generated 6 programs each (about 27 ms),
right after the first load and before that load's precompile line. Those programs came from the
cache (no compilation: the .bin was not rewritten), but they were generated during play.
