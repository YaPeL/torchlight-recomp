# perf/shader-cache on Direct3D 11: the program cache and the precompilation

Windows, 2026-10-17. RTX 5080, 3840x2160, D3D11. Release builds from scratch (SDK
D:\rexglue-sdk-33). Isolated profile (`--user_data_root`, a copy of the plain test save, no mods).
The route each session is start, Continue, town, into the mine, one floor, quit from the menu.

The cache folder is still `%LOCALAPPDATA%\TorchlightRecomp\ogre`, because `--user_data_root` does
not move the cache kind (noted as pending isolation work). Before the first session of each build,
only `microcode-*.bin` and `programs.txt` were deleted there; the RTSS's `.hlsl`/`.glsl` sources
stayed. `session-N-cache.txt` records the files before and after each session (size, write time,
SHA-256 prefix). The rest is as in `README.md`: `analyze.py` output, log excerpts. `analyze.py`
now also lists the precompile lines, the OGRE log's program cache lines and the title screen's
frame with the most program time (the reported frames in the first 15 s).

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
