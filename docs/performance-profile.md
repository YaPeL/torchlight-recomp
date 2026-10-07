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

**Open**: the native renderer still has a few long frames with warm caches (209.8 ms on arriving in
the town, 190.5 ms in a fight): not shader compilation, to be traced.

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
