# Gameplay performance profile — 2026-09-12

## Native renderer against Xenos, again (2026-10-14)

The comparison of 2026-10-07 (next section) repeated with everything taken since: the backend's
cuts, the D3D skip, the game thread's steps 3a and 4, the frustum test as host code, and SDK patch
33 (which applies to both renderers). Develop `0aec8a7` with the step overlay (local), the same
executable in both modes, the reference route played by hand (the fixed-floor saved game: the
mine standing still and fighting, the stairs, the town square; not the 2026-10-07 route of a new
character), no cap, no vsync, 720p, warm shader caches, the 55 C cool-down before each run; one
run of each mode. Xenos draws with Vulkan, which chooses its own device: the runs force the NVIDIA
one (`VK_ICD_FILENAMES`, as the 2026-10-07 script did); without it the loader picked the Intel UHD
630, which gave the Intel pair below.

GeForce GTX 1050 Ti, the game's frame rate (presented, native only), mean over the step:

| Step | Native | Xenos | Native over Xenos |
|---|---|---|---|
| Mine, standing still | 239.3 (180.5) | 63.7 | 3.76x (2.83x presented) |
| Mine, fighting | 187.5 (175.4) | 51.6 | 3.63x (3.40x) |
| Town square | 149.6 (142.9) | 43.8 | 3.42x (3.26x) |
| Frames over 33 ms (per step) | 0, 1, 0 | 0, 8, 42 | |
| Level loads (Continue; the stairs) | 3951, 4225 ms | 5426, 9076 ms | 1.4x, 2.1x |

Intel UHD 630 (the native backend on GL, Xenos on Vulkan), the game's frame rate:

| Step | Native | Xenos | Native over Xenos |
|---|---|---|---|
| Mine, standing still | 208.1 | 28.0 | 7.4x |
| Mine, fighting | 165.1 | 26.2 | 6.3x |
| Town square | 138.8 | 26.3 | 5.3x |

On the NVIDIA GPU the native renderer went from 1.9-2.5 times Xenos's frame rate (2026-10-07) to
3.4-3.8 times; the route differs, and each mode ran once, but the change is far beyond the runs'
spread (3-10 %). On the Intel GPU the native renderer loses little (CPU-bound: 208 against 239 fps
standing still) while Xenos falls to 26-28 fps. The package sat at 82 C in every step of every run
(3080-3740 MHz).

## Against the PC version through Proton (2026-10-14)

A rough reference, not a like-for-like comparison: Torchlight for PC (OGRE 1.6, Direct3D 9, the PC
data) run through Proton with DXVK, in an isolated prefix, against this port's native renderer. The
same reference route by hand from the same saved game (the fixed-floor save converted to PC), 720p
fullscreen, no cap, no vsync, the 55 C cool-down; the PC's frame rates come from one MangoHud log
of the whole run, cut at the two level loads, 0.5 s trimmed at each end. They are presented frame
rates, so they go against this port's presented column (the previous section's runs, develop
`0aec8a7`). One run of each.

| Step | PC, NVIDIA | Port, NVIDIA (presented) | PC, Intel | Port, Intel (presented) |
|---|---|---|---|---|
| Mine, standing still | 184 | 180.5 | 186 | 208.1 |
| Mine, fighting | 177 | 175.4 | 145 | 164.6 |
| Town square | 172 | 142.9 | 145 | 138.5 |

Comparable overall: the PC version is ahead in the town square on the NVIDIA GPU (+20 %) and
slightly on the Intel one (+4 %); this port is ahead in the mine on the Intel GPU (+12-14 %) and
even in the mine on the NVIDIA one. Frames over 33 ms in the fight and the town square: PC 14 and
9 (NVIDIA), 13 and 8 (Intel); this port 1 and 0 on both. The level loads are not compared (the
port's are the guest's own load, the PC's the gap in the frame log). On the NVIDIA GPU this port's
presented rate is below its own game rate (239 against 181 standing still): its backend thread
spends 2.6-3.3 ms per frame in the present, against 0.4-0.5 ms on the Intel GPU, which drives the
panel; the PC version through DXVK presents at the same rate on both GPUs.

## Native renderer against Xenos (2026-10-07)

The same build of the game run twice with each renderer: the native backend (`--native_live=only`,
OGRE GL3+) and the SDK's emulated GPU (`--native_live=off`, Xenos on Vulkan). Not against Xenia.

**Machine**: Lenovo ThinkPad X1 Extreme, Intel Core i7-8750H (6 cores, 12 threads), NVIDIA GeForce
GTX 1050 Ti with Max-Q Design (driver 580.178.04, GL for the native backend, Vulkan for Xenos), 30
GB RAM, Ubuntu 26.04 (kernel 7.0), Wayland, plugged in, nothing else open, no MangoHud.

This laptop has two GPUs (Optimus). Its panel (eDP-1, 1920x1080 at 60 Hz) is wired to the Intel
UHD 630; the NVIDIA's own outputs (HDMI, two DisplayPort) are unconnected. The native backend draws
on the NVIDIA (EGL on Wayland, GNOME), so every presented frame is copied to the Intel to be shown
(PRIME render offload): part of the present's 2-4 ms in the town square is that copy. Machines with
one GPU, or the monitor on the GPU that draws, do not pay it, and they are limited by the CPU too.

**Since 2026-10-09 every measurement reports two frame rates**: the game's (guest swap to swap,
`frame time (guest swap to swap)` in the log, what the step overlay always measured) and the
presented one (present to present on the backend thread, `frame time (presented, present to
present)`, with the guest frames dropped because the backend was behind). When the backend is the
limit, the second is the lower one and is what the player sees; the frame counter (F3) shows both.

**Since 2026-10-09, every measurement also records the machine's state**, because alternated runs
of the same binaries drifted run after run (develop's town square 130 -> 133 -> 139 -> 146 fps
while phase A's went 138 -> 137 -> 135 -> 132), which looks thermal. Every 2 s during each run:
the CPU frequency (mean, min and max over the cores), the package and hottest core temperatures,
the fan, whether the charger is plugged in, the kernel's thermal throttle counters (per core and
package, from `/sys/devices/system/cpu/cpu*/thermal_throttle`), and the GPU's temperature, clock,
power and utilisation (`nvidia-smi`). The power profile (`powerprofilesctl`), the governor and the
energy performance preference go in its header. Each run's report puts them next to the frame
rates of every step: mean frequency, mean and highest package temperature, throttle events during
the step. Measurements run with the power profile set to **performance** (intel_pstate, EPP
`performance`) and the charger plugged in. No frequency cap: on this laptop the package sits at
82-83 C with the CPU near 3.0 GHz while the game runs, throttling all the time (tens of thousands
of package throttle events per 40 s step), so before each measured run the machine cools until
the package temperature (`x86_pkg_temp`) stays below **55 C** for 10 s (it idles at 45-50 C).
Even after the cool-down the package reaches 82 C within every measured step (the step-b runs of
2026-10-10: 82 C in all of them, thousands of throttle events per step): this laptop measures under
its thermal limit, so the CPU clock follows the temperature, and small gains stay within the noise.
Frame rates of such runs still move by several percent between identical runs; decisions on small
changes also use the profile (the change's share of the thread it runs on), which the drift does
not move.

**The reference route (since 2026-10-10)** is played by hand, on the fixed-floor saved game, with
the step overlay giving the steps: Continue from the main menu; the mine's first floor standing
still (5 s), then fighting west over the bridge to the spiders and the NPC (40 s); the
stairs up to the town; the town square walked around on the player's usual route (40 s). It is
more demanding than the scripted `measure-fight-town` (the town square at 138 fps by hand against
higher figures scripted), so scripted runs are only compared with scripted runs. A recording of
the hand route played back as an input script does not replace it: the game's randomness (where
the monsters move during the fight) leaves the character elsewhere when the fight ends, and from
there the recorded input no longer does the same thing. In the one playback tried, the dungeon
steps came close (fight 166 fps against 170 by hand), but the character never reached the stairs,
and the recorded town input ended up in the menus.

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

### Bucket culling (2026-10-08)

`--native_bucket_cull` (on by default, `=false` turns it off; `docs/guest-hot-paths.md`, "Bucket
culling") stops queueing the StaticGeometry buckets entirely outside the main camera. Fixed-floor
saved game, draw skip on, same binary with the culling off and on, three runs each:

| Fight with the NPC | Off | On |
|---|---|---|
| FPS (mean of three runs) | 123.5 (130.8, 118.6, 121.2) | 135.6 (140.3, 136.1, 130.3) |
| p99 (1 % low) | 12.56 ms (79.6) | 11.52 ms (86.8) |
| Frames over 50 ms (all runs) | 4 | 5 |

About 53 of 143 buckets per frame are dropped in the fight. In the town square almost none
(~1 of 130): 106.8 against 108.2 FPS, within the run-to-run spread. The frames over 50 ms are the
guest's own (the long frame report).

Per piece (`--native_bucket_cull_pieces`, one box per connected piece of a bucket, at most 16),
against one box per bucket, same binary, two runs each, step overlay with 40 s in the fight and
40 s in the town square:

| | Per bucket | Per piece |
|---|---|---|
| Fight, FPS | 134.9, 137.3 | 138.8, 138.8 |
| Fight, p99 (1 % low) | 11.65, 11.30 ms (85.8, 88.5) | 10.58, 10.81 ms (94.5, 92.5) |
| Town square, FPS | 111.1, 111.6 | 121.6, 113.9 |
| Town square, p99 (1 % low) | 12.71, 12.03 ms (78.7, 83.1) | 11.19, 12.67 ms (89.4, 78.9) |
| Town square, buckets dropped per frame | 0.3, 0.2 (of ~130) | 21.2, 23.0 (of ~126) |

The fight's 1 % low gains about 7 %. In the town square both runs drop the same ~22 buckets, but
the frame rate differs by 8 fps between them: about +6 % on average, inside the square's
run-to-run spread for the 1 % low. Building the pieces costs 51 ms in total on entering the town
(27 ms with one box per bucket), spread over the loading frames, which already take 400 ms or
more; none is built while playing.

### Producer cuts (2026-10-09)

The producer was 22 % of the guest render thread in the town square's DWARF profile (develop with
per-piece bucket culling, 2026-10-08): reading the guest state for every RenderSystem call has to
stay on that thread, so the cuts are to what the producer does around those reads. Each is its own
commit (perf/producer-cuts-2):

| Cut | Profile share before |
|---|---|
| RenderSystem call counters: a relaxed load and store, not a locked increment | ~1 % |
| Per-hook timing (ProducerTimer, HookTimer, their summaries) off unless `--native_producer_timing`; the frame time statistics and the slow and long frame reports stay on | ~2 % |
| The guest std::map walk (ForEachNode) on a fixed host stack, not a vector per call | ~1 % |
| The resource registry's lookup cache invalidated per bucket of addresses, not whole on every creation or destruction | ~1.5 % |
| Flat hash maps (capture/flat_map.h) for the per-draw lookups: content versions, live buffers, the sets of what the live stream holds | ~2.8 % |

The vectors copied into each SetConstants (the constant ranges' data, the auto constants) stay:
they belong to the command handed to the consumer thread. The state shadow stays a
std::unordered_map: the argument memos keep pointers into it.

Fixed-floor saved game, step overlay, draw skip on, two runs each (the bubble monsters cast their
lightning in all four fights):

| | develop | Producer cuts |
|---|---|---|
| Town square, p99 (1 % low) | 11.12, 11.10 ms (89.9, 90.1) | 10.49, 10.45 ms (95.3, 95.7) |
| Town square, FPS | 126.3, 126.7 | 133.9, 134.9 |
| Fight, p99 (1 % low) | 9.95, 9.72 ms (100.5, 102.9) | 9.20, 8.77 ms (108.7, 114.0) |
| Fight, FPS | 156.7, 160.8 | 170.8, 167.9 |
| Standing still, FPS | 194.7, 203.2 | 209.3, 214.9 |

About +6 % in the town square (FPS and 1 % low), +7 % FPS and +9.5 % 1 % low in the fight, and
+7 % FPS standing still: some 0.5 ms less per frame on the guest's render thread, as the profile
shares predicted.

### Producer, phase A (2026-10-09)

What was left of the producer after those cuts, from the town square's DWARF profile (develop
320ee22): 21.9 % of the guest's render thread inside our hooks. Four cuts were tried, each its own
commit; two are kept. The same profile with them (35 s of the town square, phase A with all four):

| Function (share of the guest's render thread) | Before | Phase A | Cut |
|---|---|---|---|
| CaptureVertexDeclaration | 1.63 % | 0.79 % | A4, kept |
| CaptureDraw | 4.23 % | 3.47 % | A3, kept |
| ReadVertexBufferBinding | 1.40 % | 0.87 % | A3, kept |
| ReadConstants | 3.95 % | 4.03 % | A2, dropped |
| MemoState (the state memos' early out) | 2.55 % | 2.91 % | A1, dropped |
| All our hooks | 21.9 % | 20.6 % | |

About 3000 samples fall in the producer, so each row moves by about +-0.15 % on its own.

- **A3, kept**: one lookup per buffer of a draw. A draw's vertex and index buffers were looked up
  in the registry (whose small direct-mapped cache often missed), then twice more in the live
  buffer table, and the vertex buffer binding looked them up again. `Session::DrawBuffer` keeps the
  registry's answer and the live state per buffer address while `ResourceRegistry::Stamp` for the
  address is unchanged; a buffer freed and created again at the address is a new generation and
  starts from a fresh live state.
- **A4, kept**: the declaration's registry answer kept the same way, and in the live mode the
  element bytes the live stream has: the same bytes reuse their hash instead of hashing again (a
  declaration changed in place has other bytes and is hashed and described again).
- **A1, dropped**: the state memos (`Session::Unchanged`) were one entry per (slot, sub) pair, 1.7
  MB, and the assumption was that most lookups missed the cache. Packing the pairs in use behind a
  32 KiB index changed nothing: `Unchanged` kept 1.3 % of its own plus 0.5 % in `memcmp`. The cost
  is not in that table; where it is (the shadow entry each memo points to, or simply the number of
  calls, ~965 per frame for the texture filtering alone) was not measured.
- **A2, dropped**: every constant range of a `SetConstants` held its own vector, and the
  assumption was that one array of values per command would take the allocations away. They moved
  instead: the command's array and its vector of ranges grow as ranges are added, and `AddRange`
  shows the same `malloc` and `free` as before. Not tried: reserving from the number of ranges,
  which is only known after walking the guest's map.

In the game (fixed-floor saved game, step overlay, draw skip on, eight runs alternated, all four
cuts): the dungeon fight +6.5 % FPS (159.3 -> 169.7), +5.5 % presented, standing still +8 %; the
town square no measurable change (137.0 -> 135.5 FPS), with develop's runs rising run after run
and phase A's falling, the drift that led to the thermal record above. The profile is what decided
it.

**Phase B, evaluated and not done**: moving off the guest's render thread what does not read the
guest's state (the hooks would copy the raw words they read into a ring, and another thread would
build the commands). From the same profile, of the producer's ~19 % of that thread, ~2.6 % reads
guest memory (the reads, the walks of its `std::map` trees, the content copies) and ~14.9 % is host
work. Most of that host work has to stay anyway:
- the resource identities (registry lookups) must be taken at the call: a buffer freed and created
  again at the same address would be the wrong object to a thread that looks it up later. That
  keeps nearly all of CaptureDraw, the texture, program and declaration lookups (~5 %);
- the content versions decide whether a buffer is copied before the guest writes it again;
- the state memos' early out (2.9 %) would be replaced by copying every call's words into the
  ring, which costs about what the comparison saves.
What could move (the state shadow, building and appending the commands, the constant commands'
allocations) is ~4-5 % of the thread, ~0.35 ms per frame; the ring costs ~0.1-0.15 ms for some
5000 calls a frame. Net ~0.2-0.25 ms (~3 % of the thread), with one more busy thread on a machine
where that already showed in the guest's frame rate (the backend cuts above), and a large, risky
change: every hook split in two, ordering with the registry's creations and destructions from
other threads, the F9 capture kept working. Not worth it next to the guest's own hot paths.

If the producer is taken up again, what is left on its own thread:
- the constant commands' allocations: reserve the command's arrays once (A2 moved the allocations
  instead of removing them; the range count is only known after walking the guest's map, so count
  first or reserve from the last command of the same parameters);
- the state memos' cost: `Session::Unchanged` keeps 1.3 % of its own for a comparison of a few
  words, which the table's size did not explain (A1). Measure where it goes (the shadow entry each
  memo points to, or the ~965 calls a frame of the texture filtering alone) before changing it.

### Animation controllers walked on the host: tried, no gain (2026-10-09)

`0x821C8C00` is OGRE's `ControllerManager::updateAllControllers`: once a frame it walks the
`std::set` of controllers (~860-920, all enabled) and makes three virtual calls for each, the
source's `getValue`, the function's `calculate` and the destination's `setValue` (by RTTI in a run:
frame time sources; passthrough, scale, animation and waveform functions; ParticleUniverse's
particle system update, texture coordinate and texture frame destinations). In the town square it
took 3.65 % of the guest's render thread: the walk itself (0.99 %) and the set iterator's increment
(0x824C6960, 0.51-0.64 %), and the rest in the controllers' own code, mostly ParticleUniverse's
`setValue` (0x821C8A58) and the particle update it calls (0x821C8618).

The assumption was that the walk's cost was the recompiled code's (registers kept in memory,
indirect dispatch), so the walk was moved to host code (branch `feature/native-controllers`, not
merged): the same frame test, the set in order with the next node found after each update, the
three calls left to the guest's own functions. Same binary with `--native_controllers` on and off,
the town square's profile (~17,400 samples of the guest's thread each):

| | Guest walk | Host walk |
|---|---|---|
| `0x821C8C00` with everything it calls | 3.66 % | 3.61 % |
| The walk (loop and iterator) | 1.63 % | 1.34 % |
| ParticleUniverse's `setValue` and below | 1.90 % | 2.17 % |

No gain past the noise. The host walk's own time lands on the guest memory loads (0.46 %,
attributed to their byte swaps) and on stepping through the tree (0.41 %): the cost is waiting for
memory, ~900 tree nodes, each with a controller and three objects and their vtables scattered over
the heap, which the host code reads just as the guest's did. Not merged. The guest's walk asks for
some of those lines itself (`dcbt` on the node, its controller and two more addresses read from the
node, at the top of each step), which the recompiled code drops; whether honouring the game's
`dcbt` in the codegen is worth it is evaluated separately.

### The game's own prefetches (`dcbt`): evaluated, not done (2026-10-09)

The recompiled code drops the guest's cache hints: ReXGlue's codegen emits nothing for `dcbt` and
`dcbtst` (`build_dcbt` and `build_dcbtst` in `src/codegen/builders/system.cpp`, "no semantic
effect"), with no option to change it (`dcbz`/`dcbzl` do become a `memset` of the line). Turning them
into host prefetches would be an SDK codegen patch: the effective address through `REX_RAW_ADDR`
(which applies the physical offset on Windows and macOS) into `__builtin_prefetch`, with the write
hint for `dcbtst`. That would be safe, since a prefetch of an invalid or unmapped address faults
neither on x86 (`PREFETCHh`) nor on ARM64 (`PRFM`) and changes no result.

The game has few of them: 182 `dcbt` and 6 `dcbtst` in 52 functions. Those functions are hot,
though, ~11 % of the guest's render thread in the town square: the octree walk of the culling
(`0x821C7158`, 13, 3.3 % of its own), the frustum test (`0x821C74A0`, 1, 2.8 %), `0x821C28D8` (2,
1.1 %), the animation controllers (`0x821C8C00`, 5, 1.0 %), `0x821C9F68` (4, 0.6 %), and the CRT
memcpy (7 plus the 6 `dcbtst`), which already runs as a host memcpy.

Read in place, nearly all of them ask for the line one to three instructions before the load that
uses it (`dcbt r10,r21` then `lwz r9,96(r21)`; the controller walk prefetches the current node and
controller at the top of each step, not the next ones). On the Xenon, which runs in order with long
memory latency, that pays. On the hosts' out-of-order cores (x86 on PC and the Steam Deck, Apple's
ARM) the load itself starts the miss at the same moment, so a prefetch issued just before it saves
nothing. Only a prefetch issued well ahead helps, and the game has a few: a loop of the octree walk
that asks for the next iteration's element, some child pointers read later, and one clearly useful
case, a loop in `0x821C28D8` that prefetches a block 128 bytes at a time before processing it.
Estimated gain under 0.5 % of the thread, possibly none: not done. Honouring the game's hints cannot
add lookahead where the game has none either, so it would not fix waits like the controller walk's.

### The backend thread (2026-10-09)

The live mode's backend thread, town square of the DWARF profile with everything integrated
(develop 320ee22, 499 Hz, user mode): ~4.5 ms of CPU per frame, of a ~7.5 ms frame. The rest is
waiting inside the present (2.7-4.4 ms of present, 0.17 ms of it on the CPU, vsync off).

| Layer | Share | ms per frame |
|---|---|---|
| Our frontend (the command translation) | 23.5 % | 1.07 |
| Our backend (the C API side: conversions, program choice) | 19.7 % | 0.89 |
| Our frame transport (frame applied, freed, content released) | 11.1 % | 0.50 |
| OGRE (auto parameters, parameter binding, `_render`, viewports) | 16.9 % | 0.77 |
| The GL driver (draws, uniforms, viewport and state changes, swap) | 28.7 % | 1.30 |

On this machine the present's wait is the display path's pacing, not the GPU: with frames
lighter than the refresh, two frames go out per 60 Hz refresh (14.6 + 2.0 ms). That depends on the
compositor and the PRIME copy (see the topology note above), so it is not pursued here; what reaches
other platforms is the CPU work in front of it.

`replay --session RECORDING --bench [--bench_frames A-B]` measures the backend without the game:
it plays a recorded session as the backend thread does (frames read ahead on another thread, each
one presented in a window with vsync off, then handed over to be freed) and writes per-phase means,
present to present percentiles and the 1 % low (bench.txt, bench.csv). Its window is a top-level
X11 one, not the game's Wayland surface, so its present times say nothing about the game's; the CPU
work does. Its frames are heavier than live ones (the session was recorded with the backend behind,
so dropped frames' state commands pile into the recorded ones), so it compares changes rather than
predicting live times. With run-to-run noise of about +-0.15 ms per frame, small cuts are compared
by the process's user instructions and cycles over the same frames (`perf stat`).

The cuts, each its own commit (perf/backend-cuts), validated with the unit tests, the 20 parity
replays and 78 frames of the recorded session (images identical):

| Cut | Bench (town square of the session) |
|---|---|
| Consumed frames freed on a thread of their own (FrameReclaimer) | work 6.94 -> 5.54 ms (frame freeing 1.56 -> 0.00) |
| Hashed lookups for the per-draw resource maps (frontend, live content source, backend; the two looked up by prefix stay ordered) | work 5.46 -> 5.01 ms |
| The viewport left alone when the guest sets it unchanged (setDimensions marked it updated every time) | work 5.08 -> 4.91 ms |
| The program's alpha function and WVP constants found once, not by name per draw | within the noise |
| The guest's float constants in a dense array, written a range at a time (PhysicalConstants) | instructions -3.9 %, cycles -4.6 % |
| Fetch endianness swapped a word at a time | within the noise |

Altogether, base against the last cut on the same frames: backend thread work 6.94 -> 4.43 ms per
frame (5.40 -> 4.43 leaving out freeing the frames, which these heavy frames inflate; live it was
~0.2 ms), commands 4.74 -> 3.95 ms; the whole process's user cycles -7.5 %, instructions -5.2 %.

In the game: fixed-floor saved game, step overlay, draw skip on, two runs each, alternated. The
town square is the comparison (the dungeon fight is not: its rat came out only in the runs with
the cuts):

| Town square | develop | Backend cuts |
|---|---|---|
| Presented, p99 (1 % low) | 10.49, 10.47 ms (95.3, 95.5) | 10.67, 10.00 ms (93.7, 100.0) |
| Game, p99 (1 % low) | 10.36, 8.88 ms (96.5, 112.6) | 10.09, 9.03 ms (99.1, 110.7) |
| Presented FPS | 125.0, 124.3 | 133.2, 134.1 |
| Game FPS | 138.9, 140.9 | 136.8, 135.6 |
| Guest frames dropped (backend behind) | 558, 664 | 145, 65 |

What reaches the screen gains about 7 %, and the backend drops a quarter of the frames or fewer;
the 1 % lows move less than their run-to-run spread. The game's own frame rate falls about 2.6 %.
A hypothesis, not measured: with the backend presenting nearly every guest frame, it uses more CPU
(and GPU) than before, when it skipped a frame in nine, and on this laptop that is taken from the
guest's render thread.

OGRE 14.6's Vulkan render system, tried in the replay (local experiment, OGRE built with the
Vulkan render system and its glslang plugin): the RTSS programs compile, but the render system
expects a frame laid out its way. Its window needs one made outside it; a buffer upload inside a
render pass is an assertion (the backend writes guest buffers between draws); clearFrameBuffer only
sets the next pass's load colour (the guest clears parts of targets mid-frame). With the pass ended
before each upload a capture replays to the end, every draw issued, and the image is black. Making
it work means changes to that render system or a frame recorded the way it wants (uploads first,
clears as load actions), not a swap of plugins.

### Paused: a two-stage backend pipeline

With the cuts, the backend presents 99 % of the guest's frames in the town square on this laptop
(5364 of 5426 in one run): the limit there is the guest's render thread, not the backend. The
design below stays here, not started, until a machine shows the backend falling behind (the
Windows desktop, the Steam Deck; see the next section for what to measure).

Today one thread translates the commands (the frontend) and runs OGRE and the driver. The pipeline
splits it in two:

| Stage | Thread | Work | Cost (town square profile, before the cuts) |
|---|---|---|---|
| 1. Translation | a new one | `content.Apply`, the frontend, content decoding | ~1.1-1.6 ms |
| 2. Render | the current one (it holds the GL context) | the recorded calls played into the backend: our backend, OGRE, the driver, the UI, the present | ~3 ms |

- The cut is the backend's C API. The frontend calls a `BackendSink` interface instead of
  `tl_backend_*`: a direct one (today's behaviour; the replay and the pipeline turned off) and a
  recording one that writes each call into a `HostFrame` (a per-frame arena, reused). Bulk data
  (vertices, textures) is not copied: the `HostFrame` carries the `LiveFrame` that holds it, freed by
  the FrameReclaimer after stage 2.
- One frame in flight between the stages: stage 1 waits while stage 2 is busy. Frames are still
  dropped only in the guest's frame queue, which already carries their state forward.
- Latency: at most one stage (~1-2 ms) more from the guest's swap to the present.
- Before it, the frontend has to stop deciding on the backend's return values: whether a texture
  was created (drawn with it or with the fallback), whether a buffer was uploaded, the draw's
  result and notes (statistics). Those decisions move into the backend (a missing texture draws
  the fallback, a missing buffer skips the draw, as it does already); the statistics arrive one
  frame late through a counter, as `tl_backend_take_counters`.
- Commits: (1) the backend owns the fallback and missing resources; (2) `BackendSink`, direct and
  recording, with a replay mode that records and plays on one thread (images identical on the 20
  replays); (3) the live mode on two threads behind a cvar (off to start with), both stages' times
  in the log and the slow frame report, `--bench` with the pipeline; (4) the measurement, and the
  cvar's default from it.
- Risks: copying `tl_draw` (~5 KB with its 8 stages; only the stages and matrices in use would be
  recorded, ~0.1-0.2 ms per frame), reads of render targets (a sync point; not on the live path),
  statistics one frame late, and one more busy thread competing with the guest's (see the
  hypothesis on the game's frame rate above).
- More buffers instead: a longer frame queue or swap chain trades drops for latency, not more
  frames; and how many images GL keeps in flight is the driver's choice, not OGRE's.

### Backpressure at the guest's swap: synthetic runs (2026-10-10)

`--live_backpressure` (branch `feature/backpressure`, not in `develop`): the guest waits at its swap while the backend has not taken
the previous frame (up to 50 ms, for a backend that stopped), instead of making frames that are
dropped (`live/frame_queue.h`). Measured with automatic runs of `measure-fight-town`, three each,
alternated, cool-down and thermal record as in the method. **These numbers are synthetic, not the
final word**: the script's "fight" has no real fight and its walk does not reach the slow part of
the town square, so they compare off against on in the same conditions and are not frame rates of
the game. The hand run on the reference route decides.

No frame cap, vsync off (means of three runs):

| Step | Presented fps off -> on | Presented 1 % low | Dropped | Swap-to-presented latency | Game process CPU |
|---|---|---|---|---|---|
| Dungeon, still | 196.7 -> 189.1 | 132.8 -> 124.7 | 66 -> 0 | 13.3 -> 13.0 ms | 2.60 -> 2.56 cores |
| Dungeon, "fight" | 175.5 -> 178.3 | 116.5 -> 121.1 | 544 -> 0 | 16.7 -> 13.9 ms | 2.55 -> 2.52 cores |
| Town square | 168.1 -> 166.7 | 114.3 -> 110.2 | 151 -> 0 | 15.2 -> 12.8 ms | 2.48 -> 2.47 cores |

The guest waited 0.1-1.4 ms per frame; the 50 ms cap was reached only at the start. The package
stayed at 82 C either way: under the thermal limit, the CPU time the guest no longer spends goes
to the clock, not to a lower temperature.

With `--vsync=true` on the command line (which also paces the guest's swap at the display's rate,
as the frame rate cap does: "fps cap left to --vsync=true"): the guest already makes 60 frames a
second without backpressure, with 0-6 frames dropped per step, so there is nothing to save: presented 59.9 and 0.8 cores either way, the package 1-2 C lower with
backpressure (noise). The swap-to-presented latency with vsync is 42-49 ms (about three refreshes)
with or without backpressure (1-2 ms lower with it): worth its own look.

The game's pace does not depend on how many frames it makes: walking 5 s from the town spot where
`ASCEND` lands moved the camera 4.73 units without backpressure and 4.71 with it (two runs each,
captures' view matrices), with about 100 frames fewer with it. Backpressure adds no frame of
latency: the swap-to-presented latency is lower with it.

### Backpressure: the hand runs decide (2026-10-11)

Four guided runs on the reference route by hand (off, on, off, on; no frame cap, vsync off, the
cool-down; same binary):

| Step | Presented fps off -> on | Presented 1 % low | Dropped | Game process CPU |
|---|---|---|---|---|
| Dungeon, still | 191.7 -> 198.1 | 132.3 -> 122.0 | 102 -> 0 | 2.56 -> 2.44 cores |
| Dungeon, fight | 158.3 -> 151.8 (-4 %) | 98.6 -> 93.8 | 81 -> 0 | 2.44 -> 2.45 cores |
| Town square | 137.9 -> 131.2 (-5 %) | 96.9 -> 89.5 | 94 -> 0 | 2.49 -> 2.44 cores |

Each run with backpressure presented less than each run without in the fight (153.7, 149.8 against
160.1, 156.6) and in the town square (129.1, 133.3 against 136.6, 139.3); the package stayed at
82 C either way and the latency was 1-1.5 ms lower. On the real route the backend is not behind:
without backpressure it drops 1-2 % of the guest's frames in the fight (81 in 40 s), not the third
of the synthetic runs. Waiting at the swap takes away the overlap between the guest's next frame
and the backend's current one, and the presented rate falls with it. **Not taken**: the branch
`feature/backpressure` stays as it is, not in `develop`. For the same reason the two-stage pipeline
has no case on this laptop yet: the limit on the reference route is the guest's thread.

The frame rate cap (host settings `fps_cap`, 60 by default) paces the guest's swap by itself: it
sets the guest's vblank rate in the SDK and its `--vsync` (`live/install.cpp` `ApplyFpsCap`).
With a cap, backpressure has nothing to save. **The one case left that could justify it later**:
the backend's vsync on with the cap set to unlimited. The guest then runs unpaced while the
display presents 60 frames a second and drops about two thirds of them; backpressure could save
CPU and temperature there (battery on the Steam Deck). Not measured.

### Stage 2 closed: the reference run (2026-10-11)

develop `ad2f22c` as it stays (the guest D3D skip on by default: class A render states, the
texture matrix, the sampler states and the draws and bindings), the reference route by hand with
the step overlay, the fixed-floor saved game, no frame cap, vsync off, the 55 C cool-down, power
profile performance; the package at 82 C in every step:

| Step | Game fps (1 % low) | Presented fps (1 % low) | Dropped | Game process CPU | Clock |
|---|---|---|---|---|---|
| Main menu, hands off | 368.7 (274.0) | 238.3 (104.0) | 1305 | 2.7 cores | 3060 MHz |
| Dungeon, still | 225.6 (164.5) | 202.5 (135.5) | 231 | 2.6 cores | 3207 MHz |
| Dungeon, fight | 180.1 (125.8) | 167.7 (108.2) | 497 | 2.4 cores | 3164 MHz |
| Town square | 144.4 (105.3) | 141.7 (99.0) | 106 | 2.3 cores | 3163 MHz |

For comparison, the first hand run on this route (2026-10-10, the skip off by default): fight 169.5
game / 161.5 presented, town square 137.7 / 134.0. Single runs on a laptop under its thermal limit
move by several percent; the gain of the stage's last steps is the skip on by default (the hand
runs of 2026-10-11 in docs/native-skip-guest-d3d.md: +4-9 % presented).

What is left for later, each documented where it was evaluated:

- **The two-stage backend pipeline** (above, "Paused"): on this laptop the reference route is
  limited by the guest's thread, not the backend; it waits for a machine where the backend falls
  behind on the real route.
- **The buffer bindings** (`setVertexBufferBinding`, slot 85; docs/native-skip-guest-d3d.md, class
  B): weighed 2026-10-13 (below, 0.2 %); after steps c and d brought nothing measurable here, it goes with a machine
  that can show small gains.
- **Backpressure with vsync and no frame cap** (above): the one case where the guest makes frames
  that are dropped (about two thirds at 60 Hz); it could save CPU and battery (the Steam Deck).
- **OGRE 14's interface** (the optimization list, item 8): not evaluated.
- Also written down: the swap-to-presented latency with vsync is 42-49 ms (about three refreshes),
  worth its own look; the device `SetTexture` skip and step d are discarded (no gain).

### Present wait and drops on other machines: what to measure

On this laptop the present's wait is the compositor's and the PRIME copy's pacing, so it says
nothing about other machines. Whether the pipeline above is worth it there depends on whether the
backend falls behind. To find out on the Windows desktop (and later the Steam Deck), with the
settings of these measurements (`fps_cap = 0`, `vsync = false`, the dedicated GPU, 720p render
resolution, `--native_skip_guest_d3d=true`), develop at dfc354a or later:

1. **The game, about a minute standing still in a dungeon and a minute walking around the town
   square.** From the log's 10 s summaries:
   - `frame time (guest swap to swap)` and `frame time (presented, present to present)`: FPS,
     p99 (1 % low) and `guest frames dropped (backend behind)` of each;
   - `live consumer (backend thread, ms per frame)`: the phases, and `present` among them;
   - `present (vsync wait included)` in the `live:` line: mean, p95, p99, max.

   The backend falls behind if the presented FPS is clearly below the guest's, with drops above a
   few percent of the guest's frames. The present waits if its mean is more than about 1 ms with
   vsync off; then its p95 and max say whether it is steady or in bursts.
2. **The bench**, on a session recorded there (`--live_record`) in the town square:
   `replay --session FILE --bench --bench_frames A-B`, reporting `bench.txt` (the phases, presented
   p99 and 1 % low) and the distribution of `present_ms` in `bench.csv` (median, p95, max). It runs
   in a window of its own, so its present is comparable between machines, not with the game's.
3. What to send back: the machine (CPU, GPU, driver, monitor refresh), the numbers above, and
   whether the game runs in a window or full screen.

### Vsync latency (2026-10-13)

With the player's defaults (frame rate cap 60, vsync on) the time from the guest's swap to the
screen was about 70 ms. Measured in parts since this stage:

- **Swap to presented**, in three parts per presented frame (`live/frame_timing.h`
  `PresentLatency`): in the frame queue until the backend takes it, the backend's work up to the
  present, and the present itself (with vsync, its wait for the display). In the live summary
  (`swap-to-presented latency`, `of it waiting in the frame queue`), the measurement scripts'
  `perf step '...' latency:` lines and a line of the frame counter (F3).
- **Swap to screen**: the window system's presentation time of each frame. Wayland's
  presentation-time protocol, in development builds with `TORCHLIGHT_PRESENTATION_TIME`
  (`platform/presentation_time.h`): the `swap-to-screen latency` line, `perf step '...' on
  screen:` (with the gaps over 25 ms between shown frames, a refresh shown twice) and "to the
  screen" in F3. Elsewhere only the first.

Where the 70 ms went (automatic runs, `measure-fight-town` with vsync at 60 Hz, this laptop on
Wayland): 35-40 ms in the frame queue, about 2 ms of the backend's work, about 14 ms in the
present's wait, and about 15 ms from the present's return to the screen (the compositor shows a
commit at its next refresh). The queue was full: the guest is paced by the SDK's vblank, a 60 Hz
clock of its own (patch 10), and the display by its own; the two never line up, so the two frames
of the queue fill and each frame waits almost two refreshes. **Synthetic numbers** (two runs each,
the steps' means), swap to screen:

| Lever | Presented fps | Swap to screen | Gaps over 25 ms per step |
|---|---|---|---|
| None (two frames queued) | 59.8-60.0 | 64-77 ms | 1-2 |
| A queue of one frame (`--native_frame_queue=1`) | 59.7-60.0 | 44-57 ms | 0-4 |
| Backpressure at the guest's swap (`feature/backpressure`) | 59.7-59.9 | no change (swap to presented 48-50 ms) | — |
| The guest's vblank aligned with each present's return | 36.6-38.9 | — | — |
| Late latch, a fixed lead of 5 or 8 ms | 31-37 | — | hundreds |
| Late latch, 11 ms | 59.7-60.0 | 40-53 ms | 2-3 |
| Late latch on the display's clock (presentation times), lead adjusted | 59.2-59.9 | 35-42 ms | 3-24 |

- **The alignment and the fixed latches** take the present's return as the display's vblank. It
  is one only while the present blocks: once the backend is early, the present returns at once,
  the phase moves later each frame and a refresh of every two is lost.
- **The auto latch** on Wayland's presentation times got 7-10 ms more than the queue of one, but
  its lead did not settle: first it kept probing down into missed refreshes (21-24 gaps a step),
  then, kept above the leads that had missed, it rose to the top (15.7 ms of 16.7) because refreshes
  are missed now and then whatever the lead (the compositor, a late guest frame). Not taken.
- **Without vsync** (no cap), the queue of one gave 4-5 ms less to the screen in the fight and the
  town square at the same presented rate, but fewer presented frames standing still in the dungeon
  in both runs (176-193 against 198-203 fps, more drops). So it applies with vsync only.

**The hand runs decide** (four blind runs, the reference route with the step overlay, the player's
defaults; 1 and 4 two frames, 2 and 3 one): swap to screen 63-76 ms against 49-55 ms, the same
presented rate (59.4-60.0) and drops; the fan and the package the same (3050-3280 rpm, 75-76 C).
The player could not tell them apart by feel. **Taken**: since `develop` (this stage), the frame
queue holds one frame with vsync and two without (`--native_frame_queue`, 0 = that rule;
`live/frame_queue.h` `FrameQueueDepth`).

**The display's own clock, last try (closed for this iteration).** With Wayland's presentation
times as the vblank clock (development builds), on top of the queue of one: the guest's vblank in
phase with the display's, its phase moved by the queue's wait (later while frames waited, earlier
at once when the backend waited for one), so each guest frame would be cut just before the backend
takes it (an SDK patch, `AlignGuestVblank`, on `feature/vsync-latency` only). Two runs against the
queue of one: 49-50 ms to the screen against 43-54 ms, the queue's wait unchanged (about 17.5 ms),
the phase never settled (11-14 ms after the display's vblank, about 85 frames a period late): the
guest's frame time moves too much from frame to frame for one phase to be both just before the
backend and not late. The criterion (10 ms less than the queue of one, the same presented rate, no
more gaps) was not met, so the display's clock is not pursued on other platforms in this iteration;
the branch keeps the code (presentation-time measurement aside, which is in `develop`).

Pending: Windows (Direct3D 11 queues up to three frames in DXGI by default; one is a candidate,
`--native_frame_latency` on `feature/d3d11-frame-latency`, draft pull request 1, measured by the
Windows agent) and macOS.

### The buffer bindings and OGRE 14's interface: evaluated (2026-10-13)

From the town square's DWARF profile of the stage's last runs (`cdq-off2`: develop with the D3D skip
on, no cap, no vsync, 10 s from the town square mark; percentages of all the process's samples):

- **The buffer bindings (slot 85, `setVertexBufferBinding` `0x821C3E78`)**: 0.20 % (0.16 % in its
  body, 0.04 % in its thunk), its device call (`SetStreamSource`) already skipped since 2026-10-07.
  The rest is its loop over the bindings and the members it keeps (`mLastVertexSourceCount`, the
  buffers' tracking), which a host implementation would have to write as the guest does. The same
  order as the device `SetTexture` and step d (0.10-0.15 %), which brought nothing measurable on
  this laptop: **not done**; it waits for a machine that can show gains that small.
- **OGRE 14's interface**: the backend's thread is 29.4 % of the samples: our translation and
  frontend 9.8, the NVIDIA GL driver 9.5 (`libnvidia-eglcore` 8.6, EGL and the dispatch 1.0), OGRE
  5.4 (`OgreMain` 3.2, the GL3+ render system 2.2), the C library 2.5. OGRE's share has no hot
  spot: the shader constants' path (`GLSLSeparableProgram::updateUniforms`,
  `GpuProgramParameters::_updateAutoParams`, `bindGpuProgramParameters`) 0.9, the vertex bindings
  (`bindVertexElementToGpu`, `GLVertexArrayObject::bindToGpu`, `VertexBufferBinding::setBinding`
  and its map) 0.5, the camera and frustum per draw (`Frustum::updateFrustumImpl`,
  `isViewOutOfDate`, `setCustomProjectionMatrix`, `Matrix4::inverse`) 0.4, `_render` 0.1. What
  OGRE 14 offers instead (shared parameters in uniform buffers, fewer automatic parameters, one
  camera update per pass instead of per draw) would take a part of those 1.8 points of a thread
  that is not the limit: on the reference route the guest's thread is (stage 2). **Not done**; if
  a machine shows the backend behind (the two-stage pipeline's condition, above), these three are
  the first places to cut, before the pipeline, and the driver's 9.5 points (the number of GL calls)
  come with them.
- **The two-stage pipeline** stays paused for the same reason: no machine measured so far shows the
  backend behind on the reference route.

### The guest's thread is the limit; codegen with r0, r2, r11, f0 in locals: not taken (2026-10-13)

**Where the limit is.** Per thread, without vsync or a cap, on the reference route (automatic runs,
`/proc` sampled every 0.5 s): the guest's main thread (`Main XThread`, the recompiled PPC code) at
99.8-100 % of one core in every step; the backend (`tl backend`) 62-68 %, the SDK's `GPU Commands`
39-49 %, the UI thread 28-32 %, the rest under 2 % each. It hardly waits: 0.3 ms a second for the
GPU's progress (`guest GPU waits:` in the log), about nothing on our locks (0.005 ms a frame on the
frame queue). One thread computing is the limit, which is why a much faster desktop is only as much
faster as its single core. Of that thread (town square DWARF profile): the recompiled guest code
79 % (flat: the octree walk `0x821C7158` and `Frustum::isVisible` `0x821C74A0` at 1.8 % each are
the top), our producer 8.4 %, the C library's allocator and `mem*` 4.6 %, `fma` through libm 1.6 %,
the SDK's runtime 1.5 %, our hooks 1.1 %, the register save and restore helpers 0.2 % (4.6 % before
the non-volatile registers went into locals). The floating point mode switches (`ldmxcsr`) are
about 0.02 %.

**SDK patch 32** (`sdk/argument-locals`, the ReXGlue agent's; not in `patches/series`):
`non_argument_in_context` lets `non_argument_as_local` keep r12 in the context (the SEH funclets
and the stack probe read it outside the ABI) and put r0, r2, r11, f0 and v32-v63 in locals, 23 % of
the generated code's register references to the context. A live-in survey of the 43,927 functions
found those registers live at entry only in two dead tails. A checking mode
(`verify_non_argument_locals`) compares, at each read after a call, the local with what the context
would hold, and logs the first difference per site:
- three automatic routes with it (the fixed-floor game: a fight, `FAME`, two `LEVELUP`, `ASCEND`
  and its save; that save loaded, `DESCEND`, a fight; the town by the vendors, an NPC, the
  inventory): one line, `subfe r10,r11,r11` at `0x824BFEAC`, whose result does not depend on r11
  (`~x + x + CA`): the compiler's way to turn a carry into a mask. The check now skips repeated
  operands of `subfe`, `subf`, `subfc`, `xor`, `eqv` and `andc`.
- **synthetic** (three runs each, alternated): game fps +3.6 % main menu, +3.2 % dungeon still,
  +2.7 % fight, +1.1 % town square, the runs' ranges overlapping; the executable 3.5 % smaller.
- **by hand** (four blind runs, two each, the reference route, no vsync or cap): dungeon still
  227.9 -> 226.9 fps, fight 170.2 -> 177.3 (one run with it lacked the lightning effect; without it,
  between the two without), town square 141.7 -> 140.8. No visible gain.

**Not taken**: only what the hand runs back goes in. The patch, its checking mode and the filter
stay on `sdk/argument-locals`; `~/rexglue-sdk` was not swapped. This laptop measures at its thermal
limit (82 C, throttling throughout), where a 1-3 % gain is below what it can show: to measure again
on a machine without that limit (the Windows desktop, the Steam Deck). The interprocedural
liveness analysis that would also take the argument registers (r3-r10, about 60 % of the context
references; at best about +3-8 %, weeks of codegen work upstream) is left for after the producer,
if the guest's thread is still the limit.

### Producer: the state early out and the constant ranges' allocations (2026-10-13)

The two cuts left from phase A, on `perf/producer-small-cuts` (not in `develop`):
- **The early out's cache misses.** `Session::Unchanged` read each shadow entry's presence and
  version through a pointer to the entry, a command payload of about 1.5 KB: in the town square's
  annotated profile 47 % of its samples were that load and 21 % the memo's pointer. Each entry now
  keeps its version (0 while erased) in one dense array and the memo keeps its slot there.
- **The constant ranges' vector** was grown by doubling; it is reserved at its first kept range from
  the last count of the same kind (a command whose ranges are all left out still allocates nothing).

Profiles of the town square (one run each): `Unchanged` 0.67 -> 0.46 % of the process's samples,
`_int_malloc` 0.75 -> 0.62 %, `ReadConstants` 0.47 -> 0.57 %; about 0.25 points, 0.5 % of the
guest's thread. Synthetic runs (three each, no vsync, no cap), game fps: dungeon still 221.8 ->
220.2, fight 205.0 -> 198.5, town square 181.5 -> 182.4, within the runs' spread (3-10 % between
runs of one binary). **Not taken**: they do what they aim at, but 0.5 % is below what this laptop
can show, by hand or scripted. Phase B (the producer off the guest's thread, estimated 1.5-2 %)
is left for the same reason; both wait for a machine without a thermal limit.

An alternative take on the same two costs, from phase A, is on `perf/producer-a-full` (not in
`develop`, **not measured**): `fee774a` packs the state memos behind a small index table (only the
slot and sub pairs in use get a memo, instead of a 1.7 MB table with one per pair), and `66b6b43`
gives each `SetConstants` one array of values that its ranges point into, instead of a vector per
range (the capture format is unchanged). Its other commits are in `develop`.

`66b6b43` measured (2026-10-14, branch `perf/constants-array`, on develop after the game thread's
steps 3a and 4): in the fight's frame-pointer profile `capture::ReadConstants` was 3.8 % of the
game's thread inclusive, about 2 points of it the per-range vectors (`_M_assign_aux`, `malloc` and
its family, `memset`). Same images (the 20 parity captures byte for byte, 12 frames of the town
walk's session) and the unit tests green. Game-thread CPU per guest frame, three scripted runs of
each, interleaved, the package at 82 C throughout: standing still 4.43 -> 4.47 ms, the fight 4.71
-> 4.79, the town square 5.26 -> 5.26, the menu 4.00 -> 4.01, all within the base's own spread
between runs (4.30-4.50 still, 4.51-4.82 fight). **Not taken**: what it removes (at most about
0.1 ms per frame) is below what this laptop shows; to measure again on a machine without the
thermal limit.

Also written down, not measured: the `std::visit` dispatch of the session's state and live append
(`__gen_vtable_impl` thunks under `Session::State` and `LiveAppend`), about 1 % of the game's
thread in the same profile. The same order as the above, so left for the same machine.

## The backend behind in the mine: three per-draw cuts (2026-10-14)

With the game thread's steps 3a and 4 in (docs/game-thread-parallelism.md), the mine standing still
became the backend's limit: the game at 215-249 fps, 190-205 presented, 7-18 % of the guest's frames
dropped. A hand profile of that step and the fight (the step overlay's marks, DWARF, the whole
process) put the backend thread at about 29 % of the samples: our translation and frontend 10, the
NVIDIA driver 7-8, OGRE 5, the C library 3.5. Three cuts, each its own commit (`perf/backend-mine`):

- **The camera and the light set only when they change.** The custom projection was set for every
  draw, which marked the frustum out of date, and the data source rebuilt it and its inverse
  (`Frustum::updateFrustumImpl`, `Matrix4::inverse`); likewise the light's node orientation.
- **A stage's constants bound only for the classes that changed** (`BindChangedParameters`):
  compared with what the stage last received for the same program, the render system gets only the
  variability classes that changed, and no call when none did (`bindGpuProgramParameters`,
  `getActiveProgram`, the uniform walk). GL3+ already skips unchanged uniforms in its own cache, so
  the gain is the calls and walks. The last values are kept per stage and forgotten when another
  program is bound: Direct3D 11 has one constant buffer per stage.
- **A draw's vertex bindings touched only where they change**, without the three allocations per
  draw.

Validation: the unit tests, the 20 parity captures replayed byte for byte, 34 frames of the town
walk's session identical. Replay bench of that session (presenting, no vsync; three of each,
interleaved, the 55 C cool-down before each): the frontend and backend commands 3.03 -> 2.90 ms per
frame (-4.2 %), the backend's draws 1.37 -> 1.30 ms (-5.1 %), lower in each of the three pairs; the
present's wait, outside the cuts, moves by more than that between runs.

The blind hand batch (fixed-floor save, no cap, no vsync, A B B A, the package at 82 C throughout):

| | Cuts (runs 1, 4) | Base (runs 2, 3) |
|---|---|---|
| Mine still, presented fps (dropped) | 179.6 (211), 173.7 (144) | 170.7 (94), 179.9 (197) |
| Fight, presented fps (dropped) | 168.5 (183), 168.1 (245) | 156.0 (229), 169.8 (494) |
| Town square, presented fps (dropped) | 142.4 (74), 140.4 (136) | 141.6 (310), 136.1 (284) |

The town square drops fewer frames in both runs with the cuts than in both without, the fight leans
their way, the mine standing still (a 5 s step, its game rate 189-222 fps between runs) shows no
difference; by feel the two were not told apart, everything above 120 fps. Taken: same images, the
work measured lower, nothing worse.

The buffer bindings of the guest (slot 85, `setVertexBufferBinding`) stay out: besides the stream
bindings it finds or creates each buffer's device copy and uploads the system-memory shadow into
it, which is what our producer reads; a host version would have to do the same, for 0.2 % of the
game's thread.

## The ~45 ms presented hitch on this laptop: the platform's (2026-10-14)

The blind hand runs of the game thread study showed, in every step, a presented maximum of 46-61 ms
while the game's own maximum stayed at 7-28 ms. The live mode's slow frame lines place it in the
present: `backend` 35-60 ms with `present` 35-59 ms, against 2-3 ms in the frame before, with no
texture, program, buffer or layout created in that frame (`textures=0`, `programs=0`) and the
content and draws at their usual cost. It is not ours:
- It comes every 10-30 s, always at the same fraction of the wall-clock second (xx.71 s) in every
  run, spaced by whole seconds, while the game's own periodic summaries fall at another phase.
- It is the same with the NVIDIA GPU (3-6 per scripted run, `present` 41-61 ms) and with the Intel
  one (Mesa UHD 630, no PRIME; 5-6 per run, 35-54 ms).
- It is the same with and without `thermal.py`'s `nvidia-smi` polling.
- The step overlay's changes and the scripted steps do not line up with it.

What both GPUs share here is the compositor and the display: GNOME's Mutter on Wayland, with our
window through XWayland, and the panel on the Intel GPU. An X11 session would separate the two, but
this Ubuntu ships no GNOME session on Xorg; not pursued. Do not read a presented maximum of about
45 ms on this laptop as the game's or the backend's; the per-step means, the drops and the p99 are
unaffected by a handful of such frames.

## Where the optimisation work closes (2026-10-14)

Taken into `develop` in this round: the game thread's steps 3a (`_setTextureBlendMode`'s member
writes as host code) and 4 (the RTSS listener's SGPass lookup as host code) with the 3b fix of the
`SetTextureBlend` records; the backend's three per-draw cuts (camera and light, constants by
changed class, vertex bindings); SDK patch 33 (the timer queue's blocking wait); and the culling's
`Frustum::isVisible` as host code (`perf/game-thread-native-2`, fa0edc5, after the user's hand
check). Game thread CPU per frame, base to steps 3+4: fight -4.3 %, town square -5.6 %; the
frustum test another -2.7 % in the town square; the blind hand test of the first batch +3 to +11 %
presented fps (docs/game-thread-parallelism.md, sections 11 and 12).

Tried and measured without a demonstrated gain, all kept in the history: SDK patch 32 (argument
locals), step 5 (`_updateAutoParams` replayed as host code), `66b6b43` (one values array per
`SetConstants`), `Matrix4::concatenate` as host code, the pathfinder's neighbour opening as host
code, and before them the controller walk as host code. Noted, not measured: the `std::visit`
thunks of the session (about 1 %). The pattern: what gained was host code replacing dispatch and
context traffic over data already in cache; what did not was loads over scattered or already
cheap data, which the host reads as the guest did.

Left for a machine without the thermal limit (this laptop's run-to-run spread, 2-9 %, hides
anything under about 2 %): the rest of the RTSS listener (the texturing, lighting and fog
updates as host code, 3.5-4 % of the thread), the layout props' forced bone evaluation (the
study's 11.2, up to 3 % in dungeons, risk M-H), the producer's phase B and `66b6b43`, and the
large one, emitting the render submission from another thread, only viable as native emission
over a snapshot of the render queue (the study's 9.3 and F2, the legacy's road, XL). The game's
thread remains the limit in the fight and the town square; the backend in the mine standing
still.

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
