# Gameplay performance profile — 2026-09-12

## Native renderer against Xenos (2026-10-07)

The same build of the game run twice with each renderer: the native backend (`--native_live=only`,
OGRE GL3+) and the SDK's emulated GPU (`--native_live=off`, Xenos on Vulkan). Not against Xenia.

**Machine**: Lenovo ThinkPad X1 Extreme, Intel Core i7-8750H (6 cores, 12 threads), NVIDIA GeForce
GTX 1050 Ti with Max-Q Design (driver 580.178.04, GL for the native backend, Vulkan for Xenos), 30
GB RAM, Ubuntu 26.04 (kernel 7.0), Wayland, plugged in, nothing else open, no MangoHud.

**Build** (the Linux release's flags, built locally): the game (`linux-amd64-release`,
`-O3 -g -DNDEBUG`; `generated/` also `-gline-tables-only -mcmodel=large -msse4.1`), the SDK's
Release libraries (`librexruntime.so`, `librexgpu-xenos.so`: `-O3 -DNDEBUG -march=x86-64-v2`,
`patches/series` up to 19) and OGRE 14.6.0 Release (`-O3 -DNDEBUG`, `tools/deps/build_ogre.sh`). No
LTO anywhere. The two renderers run the same executable and runtime; only the renderer differs
(OGRE for the native one, the xenos plugin for Xenos), both at `-O3`. For comparison: a local
`linux-amd64-relwithdebinfo` build is `-O2 -g` in every part, and the Windows release takes OGRE
RelWithDebInfo built by MSVC (`/O2 /Ob1`, limited inlining).

**What Xenos runs without**: the native backend's own optimizations (the program analysis cache,
the producer that does not re-emit unchanged state) only exist in the native path; this project's
guest-side GPU wait hook (`hooks/gpu_wait_hooks.cpp`) was limited to the native mode for these runs,
so Xenos spins in the guest's GPU wait as without the project. The SDK patches (`patches/series`)
apply to both.

**Method**: four runs, alternating (native 1, Xenos 1, native 2, Xenos 2), each on fresh copies of
the user data and settings (`--user_data_root`, `XDG_*_HOME`), with a new character each time. No
frame cap and no vsync in both (`--vsync=false`, so the guest's vblank comes every millisecond;
native: `fps_cap = 0`, the window's vsync off). 720p in both: the native backend's internal
resolution 1280x720 (`render_resolution`), as Xenos draws the guest's 1280x720 and scales it to the
window (1920x1080, fullscreen, in both). Disk caches per run, the same rule for both: run 1 starts
with none (OGRE's RTSS shaders, the SDK's shader and pipeline cache, the NVIDIA driver's shader
caches under `$XDG_CACHE_HOME`), run 2 with what run 1 of the same mode left. A temporary overlay
(not in the release) showed the frame rate and the steps, with a beep at each: main menu, hands off
15 s; a new character into the town; town, still 40 s; town, walking around the square 40 s; into the
mine (floor 1); dungeon, still 40 s; dungeon, walking and fighting 40 s. Frame times are the guest's
swap to swap (the game's own frame rate; the native backend presented all but 0-12 of every ~1000
frames per 10 s); load times come from `level load: N ms` (the guest's level load, timed in its
hook); CPU from `tools/profile_utilization.py`, all threads, sampled every second.

Frame rate (frames per second, mean over the step), p99 and longest frame (ms), and frames past 33
and 50 ms (two and three 60 Hz frames: the stutters a percentile hides):

| Step | Native 1 (cold) | Xenos 1 (cold) | Native 2 (warm) | Xenos 2 (warm) |
|---|---|---|---|---|
| Main menu, 15 s | 240.7 fps; 5.6 / 7.5; 0, 0 | 97.4 fps; 12.4 / 17.2; 0, 0 | 229.5 fps; 5.4 / 7.7; 0, 0 | 97.1 fps; 12.6 / 16.5; 0, 0 |
| Town, still, 40 s | 108.1 fps; 20.5 / 154.9; 2, 2 | 48.4 fps; 35.7 / 216.2; 47, 5 | 110.5 fps; 20.7 / 209.8; 3, 2 | 55.5 fps; 34.3 / 205.0; 30, 3 |
| Town, walking, 40 s | 81.0 fps; 15.6 / 22.5; 0, 0 | 32.3 fps; 40.3 / 46.9; 459, 0 | 75.3 fps; 18.1 / 20.5; 0, 0 | 36.9 fps; 38.4 / 41.5; 168, 0 |
| Dungeon, still, 40 s | 115.1 fps; 10.6 / 15.0; 0, 0 | 61.1 fps; 20.6 / 25.2; 0, 0 | 134.8 fps; 9.2 / 11.9; 0, 0 | 66.9 fps; 17.3 / 22.3; 0, 0 |
| Dungeon, fighting, 40 s | 90.8 fps; 16.3 / 85.4; 1, 1 | 45.2 fps; 37.3 / 148.8; 63, 4 | 99.9 fps; 13.9 / 190.5; 3, 2 | 51.4 fps; 32.3 / 320.1; 16, 6 |

Native frame rate over Xenos's, run against run: cold 2.47 / 2.23 / 2.51 / 1.88 / 2.01, warm
2.36 / 1.99 / 2.04 / 2.01 / 1.94 (in the order of the table's rows).

Level loads (`level load`), ms:

| Load | Native 1 | Xenos 1 | Native 2 | Xenos 2 |
|---|---|---|---|---|
| Main menu to the town (a new character) | 7454 | 14833 | 7604 | 12315 |
| Town to the mine's first floor | 5417 | 9260 | 5079 | 7869 |

CPU: the whole process as a percentage of one core, and the CPU time per frame it gives (process CPU
over the step's frame rate). With no frame cap both renderers keep the game's main thread at 99-100 %
of a core (it runs as fast as it can), so the process total is not comparable by itself; per frame
it is:

| Step | Native 1 | Xenos 1 | Native 2 | Xenos 2 |
|---|---|---|---|---|
| Main menu | 247 %, 10.3 ms | 180 %, 18.5 ms | 246 %, 10.7 ms | 174 %, 17.9 ms |
| Town, still | 216 %, 20.0 ms | 180 %, 37.2 ms | 218 %, 19.7 ms | 180 %, 32.4 ms |
| Town, walking | 212 %, 26.2 ms | 183 %, 56.7 ms | 205 %, 27.2 ms | 180 %, 48.8 ms |
| Dungeon, still | 223 %, 19.4 ms | 181 %, 29.6 ms | 223 %, 16.5 ms | 172 %, 25.7 ms |
| Dungeon, fighting | 216 %, 23.8 ms | 194 %, 42.9 ms | 217 %, 21.7 ms | 184 %, 35.8 ms |

Busiest threads: native, the game's main thread 100 %, the backend's thread (`torchlight`) 72-79 %
and the SDK's GPU command thread 25-64 %; Xenos, the main thread 99 %, GPU commands 32-47 % and
`torchlight` 29-40 %.

**Results**: on this machine the native renderer ran the game about twice as fast as Xenos (1.9 to
2.5 times the frame rate in every step, cold and warm), loaded the town 1.6-2.0 times and the mine
1.5-1.7 times faster, used about half the CPU time per frame, and had almost no long frames: 0-3
frames past 33 ms per 40 s step against up to 459 (Xenos, walking in the town, where its mean frame
is about 31 ms). Not measured: other GPUs (Intel and Windows pending), runs with a frame cap, and
the Windows release's OGRE build (`/Ob1`).

### The producer on the guest's render thread (2026-10-07)

A profile of the same native run (`perf record --all-user --call-graph lbr`, 999 Hz, warm caches)
showed the game's main thread at 100 % of a core and everything else with room: the backend thread
about half a core, the GPU at 13-31 % utilization. On the main thread 75-83 % was the recompiled
game, 10-12 % this project's producer (the RenderSystem hooks recording commands) and 8-9 % libc and
std containers, mostly the producer's too: re-reading resource descriptions on every bind only to
drop them as already sent, tree containers, two clock reads per lock in `MeasuredMutex`, and the
frame's command vector growing by doubling. Four changes (`perf/producer`): descriptions the live
stream already has are not read again (textures without a content snapshot and declarations, keyed
by identity with its generation, so a resource recreated at the same address is described again;
programs are not cached: no hook sees their destruction), hash containers, the lock's hold time
sampled (below), and each frame's commands reserved at the last frame's count.

Same method and build as above, one run before (`perf-1`) and one after (`perf-2`):

| Step | Before | After |
|---|---|---|
| Main menu | 225.1 fps | 258.1 fps (+15 %) |
| Town, still | 108.8 fps | 119.1 fps (+9 %) |
| Town, walking | 80.7 fps | 87.8 fps (+9 %) |
| Dungeon, still | 123.0 fps | 160.5 fps |
| Dungeon, fighting | 89.8 fps | 114.4 fps |

The producer and its libc/std time in the town, walking: 2.55 to 1.99 ms per frame (-22 %), with
the game's own code unchanged (9.3 and 9.1 ms per frame). The dungeon rows are not comparable: each
run creates a new character, and the mine's floor is generated, so the map and the fight differ;
later dungeon measurements start from a saved game on a fixed floor. Left in the producer (town,
walking, share of the main thread): the registry lookup 2.2 %, the state early out 1.3 %, malloc
about 2 %.

**Lock times in the log**: the `locks:` part of the `live measurements` summary counts each
`MeasuredMutex`'s acquisitions and contended acquisitions exactly, and its wait when it was
contended, but the time held (`held ~N ms`) is an estimate since 2026-10-07: timed on one
acquisition in 64 and counted 64 times (`kHoldSampleEvery`, `live/measured_mutex.h`). Timing every
acquisition took two clock reads each, on per-draw lookups of the guest's render thread.

**Open**: the native renderer still has a few long frames with warm caches (209.8 ms on arriving in
the town, 190.5 ms in a fight): not shader compilation, to be traced.

### The guest's memory copies on the host (2026-10-07)

In the same profile the guest's 128-byte block copy loop (0x82884644) alone took 2.4 % of the main
thread. Two guest functions copy memory: the CRT memcpy
(0x82860A50, 1213 call sites) and a large copy built on it (0x821A7138, which uploads buffers); the
evidence is in `guest_abi/guest_functions.h` (`kMemcpy`, `kLargeCopy`). Both copy front to back with
plain loads and stores, so between ranges that do not overlap the result is that of a host
`memcpy`, and `hooks/guest_copy_hooks.cpp` runs one there. The guest's own copy still runs for
overlapping ranges (where a front-to-back copy and `memmove` differ), ranges touching the device
registers (0x7F000000-0x7FFFFFFF: the SDK serves those by decoding each faulting instruction) and
ranges crossing 0xE0000000 (where the host translation adds 0x1000 on Windows and macOS arm64) or
the end of the address space (`hooks/guest_copy.h`; tests in `guest_copy_test`). Pages the SDK
write-protects to invalidate GPU copies need nothing: its fault handler unprotects and retries any
host instruction. Every mode, native and Xenos.

**`--native_guest_copy=false`** turns it off at run time: every copy runs the recompiled guest code,
as before. It compares the two with one binary, and rules the change out if something looks wrong
(a report of corrupted data, a crash in a copy).

The log's first lines say which: `guest copies: host memcpy where it is the same copy
(--native_guest_copy=true)` or `... the recompiled guest code (--native_guest_copy=false)`.

**Measured** (2026-10-07; same machine, flags, warm shader caches and release build as above; the
steps of each run start from one saved game on the first floor of the mine, so the dungeon is the
same map in every run; frame rate per step, mean of the runs):

| Step | Guest copies (2 runs) | Host memcpy (3 runs) | x86-64-v3, host memcpy (2 runs) |
|---|---|---|---|
| Main menu | 246.1 fps | 252.2 fps (+2.5 %) | 262.2 fps |
| Dungeon, still | 148.4 fps | 152.3 fps (+2.6 %) | 151.4 fps |
| Dungeon, fighting | 102.9 fps | 106.2 fps (+3.2 %) | 108.4 fps |
| Town, still | 158.0 fps | 158.3 fps | 179.6 fps |
| Town, walking | 88.4 fps | 93.2 fps (+5.4 %) | 97.4 fps |

The host copies gain 2-5 %, small but in the same direction in every step but one: in the dungeon,
still, all three runs with them (149.4-153.8 fps) beat both without (148.3-148.5); elsewhere the
ranges touch. Two identical runs differed by up to 3 % in the dungeon and 16 % in the town, still
(where the camera stops depends on where the player stands), so the town, still, is not compared.
No step changed its long frames (0-3 over 33 ms in any run). A profile of a run with them (`perf`,
as above) no longer has the guest's block copy loop.

**x86-64-v3** (the game built with `-march=x86-64-v3`: AVX2, BMI2, FMA; the SDK and OGRE unchanged)
was measured too and dropped: 0-2 % in the dungeon (within the noise), +4 % in the main menu, and
the town, still, is the noisy step. Not worth requiring AVX2 (CPUs from about 2013 on), two builds
or a launcher choosing one; the release stays x86-64-v2.

**Where the frame rate drops** (same profile): below 100 fps, in the dungeon with several enemies and
an NPC on screen and in the town walking, the game issues about twice the draws (about 340 per frame
against 180 in the dungeon, still) at about the same main thread cost per draw (33-37 us), so the
frame rate halves. The time stays where it is in the fast steps: the guest's per-object rendering
in its SceneManager (at least 39 % of the main thread), its `_setPass` (13 %), the guest's own D3D9
`_render` under the RenderSystem hooks (at least 12 %; the call graphs are cut by the LBR depth), this
project's producer (11 %) and libc (8 %). The backend thread is not the limit (4.6 ms per frame
there against 11 ms on the main thread). The guest's D3D work under the RenderSystem, which in the
native mode prepares a Xenos GPU nobody reads, is the next candidate.

### The default code model on Linux (2026-10-07)

The SDK compiles its Linux x86-64 targets with `-mcmodel=large`, which makes every call between
guest functions a 64-bit address loaded into a register and an indirect call; the game now
overrides it with the default model (`CMakeLists.txt`; `docs/guest-hot-paths.md`). The executable's
text went from 67.5 to 62.7 MB. Same method as above, `--native_skip_guest_d3d=true` in both, two
runs each, interleaved:

| Step | Large model | Default model |
|---|---|---|
| Main menu | 269.0 fps | 299.3 fps (+11 %) |
| Dungeon, still | 156.6 fps | 163.6 fps (+4.5 %) |
| Dungeon, fighting | 118.2 fps | 118.5 fps (1 % low 76.6 -> 79.8 fps) |
| Town, still | 185.6 fps | 190.3 fps |
| Town, walking | 101.9 fps | 105.2 fps |

Both runs with the default model beat both with the large one in the main menu and in the
dungeon, still; in the fight and the town the ranges overlap (within the noise of a run).

### Long frames (2026-10-07)

Since this date the log has a `long frame:` line for every guest frame past 33.3 ms, in every
mode (`live/guest_events.h`): the guest's file existence checks (missing ones, the slowest path),
reads and `XMemAlloc` calls in that frame, and the rest of it. In the native mode the live mode's
`slow frame` line has the backend's side (textures, RTSS programs, now with the generation time
apart, buffers, present).

What they showed, native and Xenos, on the fixed-floor saved game:

- **The spikes in a fight are the game assembling equipment models** on its main thread
  (`MEDIA\WARDROBE\...`, `MODELS\ARMOR\...`): 84-99 ms in the native mode, of which about 7 ms
  of `XMemAlloc` (80-120 calls), 1-2.5 ms of file checks, 0.05 ms of reads, and 76-89 ms of the
  guest's own work; the backend's side of those frames is normal (1-2 textures, no program, about
  9 ms). DWARF profiles put that work across the game's equipment code and OGRE, with no hot
  function, and this project's producer at its usual share. With Xenos the same loads take longer
  (`LEATHER_SET.MESH`: 184.7 ms against 84.2 ms): they are the game's.
- **The same model spikes again**: `WARDROBE\DESTROYER\LEATHER_SET.MESH` took 84.2 ms and, a
  minute later in the same run, 98.8 ms. Dropping and picking up the same item does not spike.
- **The town, walking**, has a sustained lower frame rate (more draws) and no frame past 33 ms
  outside the level load.
- **Present stalls**: on the test laptop the backend's present sometimes takes 30-40 ms, every 11 to
  17 s, always at the same fraction of the second across separate processes: tied to the system's
  clock, so outside the game (likely the laptop's GPU or compositor); not followed further. The
  user's desktop has drops too, so it is not the main cause.
- **File existence checks**: OGRE looks every resource up in each resource location, about 20,700
  missing files per run, about 0.5 s of the main thread over a session (each failure also logs a
  warning from the SDK). Candidate, not implemented: a generic negative cache ("this file does not
  exist") for the read-only game data locations, without touching the SDK; mods will add locations
  and lookups.

## PC reference (2026-10-07)

How far the recomp is from the original PC game on the same machine and scenes: where the PC is
much faster there is something to change on our side; where it is not, the ceiling is the game's
own design.

**Setup**: Torchlight PC v1.15 (Steam, build 95256) under Proton Experimental (11.0) with DXVK
3.1.1 (D3D9 on Vulkan), `PROTON_USE_WOW64=1` (the game is 32-bit; only the 64-bit MangoHud layer is
installed), in its own Proton prefix with Steam Cloud off for the game and the cloud files backed
up first. Same machine as above, `powersave` CPU governor (as MangoHud reports it; the recomp runs
used the same system settings). Fullscreen 1280x720, vsync off, no frame cap. Quality as close
as possible to what the native mode draws: the backend adds no anti-aliasing, so FSAA off on the
PC (set in the game's options); shadows and rim lighting on. The shadow texture was set to 512 in
`local_settings.txt` (the 360 renders two 512x512 targets in a dungeon capture; the PC default is
1024), but the game wrote 1024 back when the options were saved, so which size the first run
used (menu, still steps, town walk, drop and pick-up) is not known. The second run (fight and
armour) left the options alone and kept 512 (the game reads the setting with 512 as its default,
`0x5FE3CA`).

**Saved game**: the recomp's fixed-floor character, converted to PC
(`tools/save_convert/convert_to_pc.py`): every reference exists in the PC data; two random quests
had their dialog state fitted to the PC definitions. Each saved level keeps its explored map,
units and objects.

**Method**: MangoHud logs every frame (`log_interval=0`), one file per step started by hand and
stopped after 40 s (`log_duration=40`). Its frame time is the PC game's present to present. Same
metrics as the recomp's: frame rate (frames over the summed frame time), p99 frame time and its
rate (1 % low), longest frame, frames past 33 and 50 ms. The native numbers are the two warm runs
of the default code model above (`--native_skip_guest_d3d=true`), mean of the two; their steps are
shorter (menu 15 s, still 10 s, fight and walk 40 s), which changes the counts of long frames more
than the rates.

| Step | Native: fps / p99 / longest / >33 / >50 | PC (Proton): fps / p99 / longest / >33 / >50 | PC / native |
|---|---|---|---|
| Main menu | 299 / 4.6 / 7.5 / 0 / 0 | 322 / 4.9 / 58.3 / 7 / 5 | 1.08 |
| Dungeon, still | 164 / 8.0 / 34.1 / 1 / 0 | 247 / 6.0 / 57.4 / 8 / 7 | 1.51 |
| Dungeon, fighting | 118 / 12.5 / 89.2 / 2 / 2 | 163 / 15.8 / 124.8 / 23 / 14 | 1.38 |
| Town, still | 190 / 7.0 / 10.8 / 0 / 0 | 247 / 6.0 / 57.5 / 9 / 7 | 1.30 |
| Town, walking | 105 / 13.2 / 22.1 / 0 / 0 | 124 / 21.2 / 176.5 / 20 / 13 | 1.18 |
| Dropping and picking up an item | (no spike) | 141 / 16.3 / 250.2 (186-250 ms on each drop or pick-up) | |
| Equipping and removing armour, 40 s | spikes of 84-99 ms (long frame lines) | 157 / 22.2 / 202.8 / 49 / 42 (100-203 ms on each change) | |

(times in ms)

**What it says**:

- **Standing still and fighting, the PC runs 1.3 to 1.5 times our frame rate** (fighting 1.38). That is the clearest margin:
  a fixed cost per frame on our side (the recompiled game logic on its single main thread, at
  100 % of a core, and the producer), not the scene.
- **Walking in the town the gap shrinks to 1.18**, and the main menu is close (1.08): with more
  of the game's own work per frame both are bound by the same thing.
- **Long frames: the recomp is smoother than the PC.** Walking in the town the PC had 20 frames
  past 33 ms (up to 177 ms) against none; dropping or picking up an item cost the PC 186-250 ms
  each time, where the recomp shows no spike. In the fight the PC had 23 frames past 33 ms (up to
  125 ms) against 1-3. **Equipment spike**: changing armour costs the PC 100-203 ms per change,
  about twice the recomp's 84-99 ms; the spike is the game's own model assembly, and the native
  mode already does it faster than the PC under Proton.
- **Every 5.0 s exactly the PC shows a frame of about 55 ms**, menu included. The period points to
  the tools (MangoHud, Proton or Steam) more than to the game; not explained, so not used for any
  conclusion (it accounts for most of the PC's long frames in the still steps).
- **The PC numbers are a floor**: Proton and DXVK translate every D3D9 call; native Windows would
  likely be somewhat faster.

**Picking up armour that goes to the backpack** (read in the PC executable, not measured): the
360 rebuilds the character's whole equipment model when such an item enters its container, even
though what it wears does not change (`sub_822A0B28` -> `sub_822DC6B0`, about 55-66 ms). The PC does
the same: its add-to-container handler `0x49B060` makes the same test (`0x4B7F90`, or item type 12,
or 20) and calls `0x4E1860` (the counterpart of `sub_822DC6B0`) and `0x489550` (of `sub_8228D6D0`);
the remove handler `0x49B4B0` too. The only difference: the 360 checks a global flag first and
defers the rebuild while it is set; the PC has no such flag. So that spike is the game's design,
on both.

**Not done**: draw calls per frame on the PC (`DXVK_HUD=drawcalls`, cancelled for now), and timing
the PC counterparts of the guest functions in the equipment spike
(`0x822DC6B0`, `0x822C08E0`) with `perf` on the Wine process: they are not identified in the PC
executable yet, and the frame times already show the PC spending longer on the same work.

## OGRE Release against RelWithDebInfo on Windows (2026-10-07)

The Windows release links OGRE built RelWithDebInfo by MSVC (`/Zi /O2 /Ob1`: only functions marked
inline are inlined; `docs/release-pipeline.md`, 5.8). The same game run with OGRE built Release
(`/O2 /Ob2`) instead, everything else equal, in the native mode on Direct3D 11.

**Machine**: AMD Ryzen 7 5700X3D (8 cores, 16 threads), NVIDIA GeForce RTX 5080 (driver
32.0.16.1088), 32 GB RAM, Windows 11 Pro (build 26200), a 3840x2160 display at 120 Hz.

**Build**: the game as the Windows release builds it (`win-amd64-release`, `-O3 -DNDEBUG -g`,
clang 21.1.8), the SDK's Release libraries (`patches/series` up to 19), and OGRE 14.6.0 built by
`tools/build-deps/windows.ps1` with `-Configs RelWithDebInfo` (`OgreMain.dll` 7.3 MB) or
`-Configs Release` (3.3 MB). Two install trees that differ only in OGRE's DLLs and plugins.

**Method**: the one above, with the same temporary step overlay (built locally, not integrated):
fresh copies of every user folder per run (`--user_data_root` and the tests' user folder variable,
as `XDG_*_HOME` on Linux), a new character each time, Direct3D 11, internal resolution 1280x720 in
a 3840x2160 fullscreen window, no frame cap and no vsync (`--vsync=false`). Runs alternated
(RelWithDebInfo 1, Release 1, RelWithDebInfo 2); run 1 of each with no OGRE shader cache, run 2
with run 1's. NVIDIA's own Direct3D shader cache lives outside the run's folders and was not
cleared, so only the very first run met it empty; both variants compile the same shaders. CPU: the
process's CPU time sampled every second, averaged over each step. A fourth run (Release 2) was not
made: the three that were already answered the question.

Frame rate (fps, mean over the step); p99 and longest frame (ms); frames past 33 and 50 ms; process
CPU (% of one core) and CPU time per frame:

| Step | RelWithDebInfo 1 (cold) | Release 1 (cold) | RelWithDebInfo 2 (warm) |
|---|---|---|---|
| Main menu, 40 s | 311.9; 3.9 / 5.6; 0, 0; 184 %, 5.9 ms | 311.6; 4.0 / 6.7; 0, 0; 182 %, 5.8 ms | 310.7; 4.2 / 6.4; 0, 0; 185 %, 6.0 ms |
| Town, still, 40 s | 146.3; 13.9 / 159.5; 2, 2; 186 %, 12.7 ms | 146.6; 14.1 / 157.7; 2, 2; 184 %, 12.6 ms | 144.8; 14.4 / 156.0; 2, 2; 187 %, 12.9 ms |
| Town, walking, 40 s | 98.3; 13.1 / 15.7; 0, 0; 180 %, 18.3 ms | 99.8; 12.6 / 16.9; 0, 0; 183 %, 18.3 ms | 99.3; 12.9 / 19.0; 0, 0; 182 %, 18.4 ms |
| Dungeon, still, 40 s | 199.7; 6.3 / 9.1; 0, 0; 178 %, 8.9 ms | 178.5; 6.9 / 8.8; 0, 0; 186 %, 10.4 ms | 161.3; 8.0 / 12.3; 0, 0; 195 %, 12.1 ms |
| Dungeon, fighting, 40 s | 134.8; 10.4 / 176.2; 3, 2; 188 %, 13.9 ms | 119.8; 11.0 / 17.4; 0, 0; 189 %, 15.8 ms | 149.9; 9.8 / 15.0; 0, 0; 192 %, 12.8 ms |

Level loads (`level load`), ms:

| Load | RelWithDebInfo 1 | Release 1 | RelWithDebInfo 2 |
|---|---|---|---|
| Main menu to the town (a new character) | 6440 | 6401 | 6338 |
| Town to the mine's first floor | 4378 | 3820 | 3925 |

**Results**: no measurable difference. Where a step repeats the same scene every run (the main
menu, the town still and walking) the two builds are within 1 % of each other in frame rate, p99
and CPU time per frame. The dungeon steps depend on where the player stands and fights each time
and vary more between two runs of the same build (199.7 against 161.3 fps standing still) than
between the builds. The loads are within run-to-run variation too. The main thread is the limit
(the process holds 180-195 % of a core in every step, as on Linux), not OGRE's code, so OGRE's
inlining does not show. The game runs correctly with OGRE Release (a whole run without errors).
The Windows release keeps OGRE RelWithDebInfo, which also keeps its symbols for crash reports.

Also seen: on this machine the startup's OpenGL 3.3 probe (`platform::CanCreateGl33Context`) takes
about 180 ms (NVIDIA's driver initializing; 4.6 ms on Windows' own OpenGL in a sandbox without a
GPU), on the critical path before the backend starts.

## Controlled follow-up results

Repeated untraced captures at the saved pond support a real RelWithDebInfo
speedup: **24.98 → 42.14 guest submissions/s (+68.7%, 1.69×)**. This supersedes
the earlier exploratory 2.53× estimate. The optimized executable remains
experimental: two successful repetitions do not clear earlier device-loss and
black-startup failures. This pass also reproduced device loss in the unchanged
Debug build after its first timed capture.

The narrow wait fix reduces actual gameplay audio-worker CPU from **72.68% to
1.77% of one core (97.56% less)**. It does **not** demonstrate a guest-FPS gain:
fixed Debug averaged **23.91 FPS**, 4.27% below the two-run baseline mean. This
small manually matched sample does not establish whether that modest difference
comes from the fix, scene variation or changing host/GPU load; zero FPS impact
and absence of a small slowdown are not established. The CPU saving is clear,
but it is not a fix for the primary guest-frame throughput bottleneck.

### Controlled method and results

Every launch starts with its own copy of the same eight-file user-data and
shader-cache seed. The seed's hashes remain unchanged; original saves are not
used for writes. The user performs two laps of the pond, returns to the saved
spot and matches camera/minimap state. This is manual scene matching, not a
deterministic replay. After explicit readiness, ten seconds of settling precede
three uninterrupted 20-second windows. All measured windows report a 1280×720
guest framebuffer, and runtime logs identify the selected NVIDIA GTX 1050 Ti
with Max-Q Design. No benchmark uses GDB or stack sampling.

`tools/benchmark_gameplay.py` reads the existing guest/host atomic counters via
read-only parent-process memory access, Linux per-thread CPU accounting, and
concurrent NVIDIA utilization. Whole-device GPU busy includes host presentation
and the desktop; it does not isolate guest GPU work. NVIDIA clock/temperature
samples, runtime logs, executable/runtime/plugin hashes and copied save hashes
are retained with each trial. Background builds and standalone CPU tests do not
overlap timed windows. The initial build comparison uses counterbalanced launch
order Debug → optimized → optimized → Debug. Two fixed-runtime Debug launches
follow. No timing, pacing or guest-copy changes are involved.

Each cell below is **mean (sample variance)** across **two independent launch
means**, each based on three windows. Variance uses the squared unit of its row
and denominator n−1. The three windows are not counted as independent runs.

| Metric | Debug / original runtime | RelWithDebInfo / original runtime | Debug / wait fix |
|---|---:|---:|---:|
| Guest submissions/s | 24.981 (0.889049) | 42.138 (0.00680893) | 23.914 (0.0199999) |
| Host presents/s | 385.881 (2426) | 230.096 (0.435782) | 438.617 (49.4867) |
| Guest main CPU, % one core | 99.474 (4.76918e-07) | 98.266 (0.000140644) | 99.466 (0.00124498) |
| Audio worker CPU, % one core | 72.677 (0.00492827) | 65.794 (0.499838) | 1.775 (0.00124987) |
| Xenos command CPU, % one core | 22.940 (0.0400679) | 32.230 (0.01999) | 22.048 (0.435492) |
| NVIDIA GPU busy, % | 74.443 (22.7812) | 87.967 (0.0545024) | 78.621 (0.255102) |

Guest FPS launch means:

- Original Debug: 25.648 and 24.314; across-launch SD 0.943 FPS.
- Original RelWithDebInfo: 42.196 and 42.079; SD 0.083 FPS.
- Fixed Debug: 24.014 and 23.814; SD 0.141 FPS.

Per-window variance and all thread CPU values are also retained in
`docs/bringup-artifacts/performance-controlled/summary.json`, regenerated with
`python3 tools/summarize_gameplay_benchmarks.py`. Only two independent launches
per configuration limits confidence in small FPS differences. Main guest CPU
remains about one core in both build types. The large compiler-build speedup
supports substantial CPU-side translated-code cost; GPU busy rising from ~74%
to ~88% suggests additional GPU pressure as throughput increases, without
identifying a specific GPU pass. Audio polling is a separate CPU waste source.
Host presentation is ~386/s in baseline Debug, ~230/s optimized and ~439/s with
the wait fix; these are explicitly not actual game FPS.

### Narrow wait fix and validation

The finite branch of `PosixConditionBase::WaitMultiple` floored remaining time
to whole milliseconds. An alertable 1 ms slice therefore spent most of its
remaining interval in repeated zero-duration sleeps. The replacement is:

```cpp
std::this_thread::sleep_until(std::min(end_time, now + std::chrono::milliseconds(1)));
```

It preserves the steady-clock deadline and requested sleep of at most 1 ms while
retaining fractional time. Object checks, consumption, locking, wait-all
atomicity, timeout decisions, infinite-wait behavior and outer APC handling are
unchanged. This is three replaced statements plus explanatory comments, not a
synchronization redesign. Durable patch:
`patches/rexglue-posix-wait-fraction.patch`.

Standalone `tests/wait_loop_test.cpp` verifies zero-timeout ordering, event
reset/consumption, wait-all failure atomicity, semaphore consumption, delayed
finite/infinite signals, APC delivery and bounded timeout behavior. Three
baseline 300 ms idle alertable waits use 99.993 / 99.991 / 99.994% of a core;
three fixed waits use 1.210 / 1.063 / 1.049%, with ~300.0 ms wall duration and all
semantics checks passing. The fixed CPU-regression threshold is 25%; assertions
must remain enabled. Example build/run:

```sh
clang++ -std=c++23 -O2 -pthread \
  -I$HOME/rexglue-sdk/out/install/linux-amd64/include \
  tests/wait_loop_test.cpp \
  -L$HOME/rexglue-sdk/out/install/linux-amd64/lib -lrexruntime \
  -Wl,-rpath,$HOME/rexglue-sdk/out/install/linux-amd64/lib \
  -o docs/bringup-artifacts/performance-controlled/wait-loop-test
LD_LIBRARY_PATH="$PWD/docs/bringup-artifacts/performance-controlled/fixed-lib:$HOME/rexglue-sdk/out/install/linux-amd64/lib" \
  timeout 10s docs/bringup-artifacts/performance-controlled/wait-loop-test --require-low-cpu
```

The SDK runtime-only build passes, with one pre-existing signedness warning at
threading_posix.cpp:788. Original runtime SHA256 is
`9e74ca7ec9a5396d41ae36eff45cd14517eb37d5bf0ba03ef65eadedad57e12a`;
candidate SHA256 is
`5d0cd9a3edf983e04d7185f9bda612d911f44a01defa07ec8932646b95a6ee10`.
Each fixed launch's process mappings confirm it loads the candidate. The
installed runtime now matches this tested candidate; the original is preserved
under `performance-controlled/baseline-lib` for comparison or rollback. Both
fixed NVIDIA trials have user-confirmed normal gameplay/controller input,
audio and F12, followed by normal process exit code 0. The Intel check also
passes gameplay/controller, audio, F12 and normal exit (code 0); runtime log
`torchlight_057.log` confirms Intel UHD Graphics 630 (CFL GT2). No Intel
performance claim is made. The installed library passes the standalone wait
semantics/CPU test again (0.923% of one core). The optimized executable has not
been promoted, and the modest measured guest-FPS difference remains a stated
limitation of this CPU-saving fix.

### Separate correctness blocker and exclusions

`debug-b` aborted with exit −6 **after all three completed windows**. Its runtime
log `torchlight_053.log` records failed Vulkan command submission at
22:20:19.377, and console reports Graphics device lost. This was the original
Debug executable and original preserved runtime, with no debugger attached and
before any game run used the wait fix. Device-loss instability is therefore
**not exclusive to RelWithDebInfo**. No native stack/core was captured in this
untraced run; the queried kernel journal contains no NVIDIA fault report.
The older optimized fatal stack remains available under `performance/`.
Completed pre-failure windows are retained for performance analysis, but this
launch is a failed stability trial. No new black startup occurred in the two
optimized repeats, and both exited code 0; the prior failure remains unresolved.

`debug-a` exited before the readiness marker and has no timed windows, so it is
excluded. `debug-c` and both fixed NVIDIA trials exited normally. The Intel
run is correctness-only, with no timed window and no NVIDIA utilization sampling.
No generated guest code, shader/renderer logic, stencil-transfer fix, gameplay
input behavior, overlay counters, pacing, resolution, UI or license handling
was changed. All measured launches retain Xenos plugin SHA256
`f7cca3412c33e56197c0eef5ca9b194e88cad616cd8671f06869d0c80a12ef7c`.

## Earlier exploratory pass

The clearest performance candidate is enabling compiler optimization for the
translated guest code. The current Debug build uses effective `-O0`; the existing
RelWithDebInfo preset uses `-O2 -g -DNDEBUG`. Building that preset increased guest
submission rate substantially at the same user-reported gameplay location.
However, the optimized validation run later suffered Vulkan device loss. **The
optimized build is experimental, not a validated replacement for Debug.** At
that stage no renderer/runtime optimization had been applied, and the Debug
binary and SDK libraries were unchanged.

## Measurements

Both captures used the NVIDIA ICD, installed SDK library path, extracted game
root, the same Xenos plugin and the existing F12 counters. The user confirmed
actual gameplay and standing at the same spot for the comparison. Each FPS
window lasted 30 seconds with no periodic debugger interruptions. GDB was still
attached and handled normal GPU write-watch signals, so these are not untraced
performance results. The test was one matched-location pair, not repeated trials
with controlled temperature/clock state.

| Metric | Debug | RelWithDebInfo |
|---|---:|---:|
| Guest frame submissions/s | 17.29 | 43.78 |
| Host successful/suboptimal presents/s | 480.45 | 217.58 |
| Main guest thread CPU, % of one core | 93.21% | 85.62% |
| Audio worker CPU, % of one core | 73.82% | 73.93% |
| Xenos command thread CPU, % of one core | 18.36% | 35.15% |
| Timer thread CPU, % of one core | 26.05% | 24.02% |
| UI/presenter thread CPU, % of one core | 16.79% | 8.40% |

The measured increase is **2.53×**. Guest main-thread CPU time per submission
drops from about 53.9 ms to 19.6 ms. The earlier informal 24–26 FPS observations
were not substituted for the measured 17.29 FPS baseline at this location.

NVIDIA GeForce GTX 1050 Ti with Max-Q Design utilization during the optimized
30-second FPS window: **89.5% average GPU busy (80–96%)**, 20.9% average memory
controller utilization, 421 MiB GPU memory, P0, 1708 MHz core / 3504 MHz memory,
66–70°C. The Debug GPU monitor started after that process exited, so no valid
Debug GPU-utilization comparison is available. High optimized GPU activity plus
a heavily occupied guest main thread suggests mixed CPU/GPU pressure; utilization
alone does not establish which GPU pass limits frame progression.

## CPU hotspots and remaining costs

A separate optimized run segment collected 60 all-thread stack snapshots at
roughly 350 ms intervals, after the FPS window. Unwinding paused the inferior for
3.18 seconds in total; that segment's FPS is excluded from the comparison.

- Main guest top frames: `sub_82860A50` 15/60, `sub_82884644` 12/60,
  `sub_82884320` 8/60. Source inspection shows guest memory-copy loops, including
  vectorized load/store paths. Their callers include rendering/buffer-update
  paths through `sub_821A71E8`. These are **stack occupancy counts**, not CPU-cycle
  shares: GDB scheduling/write-watch interception biases the sample points.
  The main thread was in ptrace-stop state before 48/60 explicit interrupts.
- Audio worker: 59/60 stacks include `PosixConditionBase::WaitMultiple`.
  `AudioSystem::WorkerThreadMain` uses alertable WaitAny; the POSIX implementation
  supplies 1 ms polling slices, then truncates the remaining sleep to whole
  milliseconds. Most of a 1 ms slice can become `sleep_for(0)`, repeatedly
  trying mutexes, checking semaphores and allocating the lock vector. This
  explains a concrete secondary CPU waste candidate. No wait/timer behavior was
  changed in this pass.
- Xenos command processing: 46/60 snapshots contain a wait path, predominantly
  the worker waiting for more commands. Other samples include binding updates,
  register writes and pipeline-cache lookup. The command thread's total CPU
  cost increases with the higher guest throughput but remains below one core.
- All 60 UI-thread snapshots are in the presentation call chain, mostly NVIDIA
  driver/ioctl paths. High host presentation is real, but its CPU use is below
  the guest main thread and audio worker. No presentation-rate cap was tested,
  because that would change pacing during this investigation.
- 230 graphics pipelines were loaded from Vulkan storage before optimized
  gameplay measurement. Shader compiler workers were waiting in the captured
  stacks; no evidence identifies steady-state shader compilation as the main
  CPU bottleneck.
- GPU timestamps/per-pass replay were not collected. Render-target transfers,
  GPU copies and repeated host composition therefore cannot be ranked by GPU
  execution time from this capture. No broad renderer changes are justified by
  these data.

## Validation blocker

The user confirmed optimized gameplay/audio/F12 checks, but then reported a hang.
GDB captured **SIGABRT / VK_ERROR_DEVICE_LOST** in the GPU Commands thread:

`vkQueueSubmit failure → VulkanCommandProcessor::EndSubmission →
GraphicsSystem::OnHostGpuLossFromAnyThread → FatalError → abort`

The presenter also logged command-submission failure at 17:19:11.798. The main
guest was in the `sub_821A5C10` GPU-progress wait path. This was not a new
InvalidFunctionTrap. The unchanged Xenos binary includes the verified stencil
fix; its SHA256 remains
`f7cca3412c33e56197c0eef5ca9b194e88cad616cd8671f06869d0c80a12ef7c`.

An isolation rerun without periodic stack sampling produced a user-reported
black screen **from startup**, before menus/gameplay. It did not capture another device-loss abort: its log shows
swapchain recreation and then the window-close/hard-exit path at 17:22:42.
The process had exited before inspection. These observations do not yet prove
whether optimization, debugger signal handling, persistent driver state or an
existing Vulkan issue caused the failures. Intel regression validation has not
been completed for the optimized candidate. Do not treat its speedup as an
approved stable optimization until the rendering failure is resolved.

The unchanged Debug/NVIDIA executable was then relaunched with the same 230
cached pipelines. The user confirmed normal startup and menu rendering. This
narrows the black startup to optimized-run conditions rather than a persistent
failure affecting both builds, but is not proof of a particular compiler bug.
The focused pass stops here with the unstable candidate unpromoted. The next
correctness investigation should isolate optimization-sensitive behavior before
further performance changes; the separate audio polling cost remains unpatched.

## Reproduction and artifacts

Artifacts are in `docs/bringup-artifacts/performance/`: `debug-baseline.json`,
`optimized-baseline.json`, `optimized-stacks.json`, `optimized-utilization.json`,
`summary.json`, runtime logs, and full GDB logs including `device-lost-gdb.log`.
`debug-utilization.json` is explicitly invalid for gameplay comparison.

`perf` was unavailable because `kernel.perf_event_paranoid=4` blocks user events
and non-interactive sudo was unavailable. No system security setting was changed.
`tools/profile_guest.py` performs bounded read-only GDB counter/stack collection;
`tools/profile_utilization.py` reads per-thread CPU accounting and NVIDIA status.

The separate optimized binary was built with the existing preset:

```sh
cmake --preset linux-amd64-relwithdebinfo -DTORCHLIGHT_RENDERDOC_INCLUDE_DIR=$HOME/rexglue-sdk/thirdparty/renderdoc
cmake --build --preset linux-amd64-relwithdebinfo -j 4
```

No generated guest source, runtime synchronization, renderer logic, frame pacing,
resolution settings, UI behavior or license handling was modified.
