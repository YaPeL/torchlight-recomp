# Profile-guided optimisation and ThinLTO

The generated code and the game's own, compiled with clang's instrumented profile-guided
optimisation (PGO) and ThinLTO (`CMakeLists.txt`: `TORCHLIGHT_LTO`, `TORCHLIGHT_PGO_GENERATE`,
`TORCHLIGHT_PGO_USE`); the SDK's libraries get neither. Started after the comparison with
torchlight-nx (docs/perf/native-render-queue.md section 8): same cut, about twice our speed per
draw, most of it from the build pipeline. The owner's decision (2026-10-30): PGO on Linux and
Android first, trained by us; Windows and macOS later, as below.

## Measured

Desktop (i7-8750H, `fight.txt` offscreen on the Intel GPU, fps cap 0, vsync off, 2026-10-30; mean
guest frame ms, A B B A each):

| Comparison | Fight | Rest |
| --- | --- | --- |
| Base against PGO | 10.47 -> 10.08 (-3.7 %) | 6.38 -> 6.18 (-3.1 %) |
| ~~Base against ThinLTO alone~~ (void) | ~~10.94 -> 10.45 (-4.5 %)~~ | ~~6.34 -> 6.26~~ |
| ~~PGO against PGO + ThinLTO~~ (void) | ~~10.21 -> 9.31 (-8.8 %)~~ | ~~6.08 -> 5.82~~ |
| ~~Base against PGO + ThinLTO~~ (void) | ~~10.70 -> 9.31 (-13 %)~~ | ~~6.36 -> 5.82~~ |

PGO alone is within the runs' spread (base fights 10.2-11.1 ms). The ThinLTO rows are void: those
builds bound 1454 of the generated code's calls past the hooks (docs/regressions.md), so they did
not run the same code. ThinLTO is out, in measurements and in the release (the owner's decision),
until the codegen makes `sub_X` a weak function calling `__imp__sub_X` instead of an alias; then PGO
+ ThinLTO is measured again from the start. The CPU ran at 2.4-3.2 GHz and 55-62 C before each run.

Android (POCO X7, `fight.txt`, fps cap 0, vsync off, 2026-10-30, A B B A, CPU 2.4-2.5 GHz): PGO
alone, without ThinLTO, takes the fight from 21.6 to 17.7 ms a frame (-18 %; the PGO runs 16.8-18.3,
the base 21.0-22.2) and the rest from 11.7 to 9.9 ms (-16 %); frames over 50 ms in the fight from
12-14 to 1-5. `-march=armv8.2-a` on top gave nothing measurable (17.8 against 17.3 ms), so the
hardware floor stays armv8-a. PGO is integrated for Android (the owner's decision, 2026-10-31).

## Measurement builds

Measurements are built with the `*-release` presets (the release's build type), or with an SDK
that has patch 53. Before patch 53, every configuration but Release counted each indirect call of
the generated code for the debug overlay (`rex::perf::IncrementCounter`, an atomic add in a call
that is not inlined): about 8 % of the guest main thread's cycles in the fight. RelWithDebInfo and
Debug builds measured that cost and the release does not have it.

Desktop (i7-8750H, `fight.txt` offscreen under Xvfb, fps cap 0, vsync off, no PGO, both builds
`-O2 -g`, develop `b704701`; `perf stat` on the guest main thread over the fight step, per frame;
A B B A twice, 2026-11-05):

| SDK | RelWithDebInfo cycles/frame | Release cycles/frame (against RelWithDebInfo) | Fight mean ms | CPU at each start |
| --- | --- | --- | --- | --- |
| Series up to 52 | 35.53 M (34.6-36.9) | 32.36 M (31.6-33.0), -8.9 % | 13.39 / 12.14 | 2.3-3.4 GHz, 63-84 C |
| Series up to 54 (patch 53) | 32.09 M (30.7-32.8) | 32.27 M (31.3-33.4), +0.6 % | 12.19 / 12.33 | 2.4-3.5 GHz, 62-75 C |

Both columns are `-O2 -g` without PGO: the table compares the build types, not the release's build
(`-O3`, with its profile). So a RelWithDebInfo measurement on an SDK before patch 53 has its absolute times about 9 % high on
the desktop, gains look smaller than in the release, and a change that removes indirect calls
looks larger. Comparisons between two such builds stay comparable with each other, not with the
release. The count can still be had for a profiling session: a Debug or RelWithDebInfo build
against an SDK with Tracy and the perf counters (`REXGLUE_ENABLE_TRACY`,
`REXGLUE_ENABLE_PERF_COUNTERS`, both on by default), configured with
`-DCMAKE_CXX_FLAGS=-DREXGLUE_PROFILE_INDIRECT_DISPATCH` (patches/README.md, 53); the overlay's
Dispatch line shows it.

The profiles were not affected: every training build was Release (Linux and Android here: their
training trees' `CMAKE_BUILD_TYPE`; Windows and macOS: the release's presets), where the count was
compiled out before patch 53 too. Patch 53 changes `codegen_id`, but not what Release compiles, so
the profiles still match without training again. Checked on Linux: develop `b704701` built as
the release (`linux-amd64-release`, `-O3 -g`, x86-64-v3) with `pgo/linux-x86_64.profdata` against
an SDK with the series up to 54 links (the generated code is compiled with
`-Werror=profile-instr-out-of-date`), and `tools/pgo/check_profile_match.py` gave 4 of 19665 functions mismatched, all in the
game's own code (v1.0.0's build: 5).

## Making a profile

1. Build with `-DTORCHLIGHT_PGO_GENERATE=ON` (the same compiler as the release: the profile format
   follows the LLVM version).
2. Play the training scripts, each from a fresh copy of the bench save, with `--dev_input_script`
   and `--pgo_profile_dir=DIR`: `fight.txt`, `fight5.txt` and the town (`town.txt` on Android,
   `tools/input_scripts/measure-fight-town.txt` on the desktop). The counters are written every
   minute (`src/pgo/pgo_writer.cpp`: the game ends by a hard exit), so a run must last more than a
   minute; each write replaces the run's file with the counts since the start.
3. Merge with that compiler's `llvm-profdata merge --sparse -o PLATFORM.profdata DIR/*.profraw`.
4. Build with `-DTORCHLIGHT_PGO_USE=PLATFORM.profdata`. The build stops by itself when the profile
   is missing (configure) or when any generated function no longer matches it
   (`-Werror=profile-instr-out-of-date`), and rebuilds everything that reads it when it changes.
   `tools/pgo/check_profile_match.py LOG --build-dir BUILD --profdata LLVM_PROFDATA` on the build's
   output also measures the game's own code: it fails over 5 % of the profile's functions with
   mismatched data, or when the profile was not used.

One profile per platform: the generated code differs between them, and any change to what the
generated code computes can change the hash of nearly every function (it happened when
`REX_PHYS_HOST_OFFSET` lost its comparison: patch 52). Any ReXGlue update or codegen change makes every profile stale at once, so
training again is part of every freeze.

| Platform | Compiler and merge tool | Who trains, where |
| --- | --- | --- |
| Linux x86-64 (also the Steam Deck) | clang 21 (apt.llvm.org, the release's), `llvm-profdata-21` | us, on the owner's desktop |
| Android arm64 | NDK r30 clang, its `llvm-profdata` | us, on the POCO X7 (`files/pgo`, pulled with `run-as`) |
| Windows x86-64 | clang 21.1.8 in GNU mode for the MSVC target (`windows_toolchain.ps1`), `C:\llvm\bin\llvm-profdata.exe` (21.1.8) | the owner's Windows PC, by its agent |
| macOS arm64 | AppleClang 21.0.0 (21000101) of Xcode 26.6, the release runner's (`macos-26-arm64`; pinned in `release.yml` with `DEVELOPER_DIR`), `xcrun llvm-profdata` of the same Xcode (AppleClang's profile format does not mix with upstream LLVM's) | the owner's Apple Silicon Mac, by its agent |

## When to train again

A profile fits one generated code: train again after any codegen or ReXGlue update (new
`generated/`), a change of the compiler (NDK) that reads it, or of the target CPU level (`-march`:
the x86-64-v3 Linux build has its own profile). The generated code's build fails
when its functions no longer match the profile (`-Werror=profile-instr-out-of-date` on
`torchlight_recomp`); the game's own code, which changes every day, only warns and loses the
profile for the functions that changed, so its share of the gain fades until the next training. A
large change of what the game does per frame (a new native path) also calls for a new profile.

Android's training, from a build with `-DTORCHLIGHT_PGO_GENERATE=ON` packaged with
`tools/android/build_apk.sh`: the bench save copied fresh before each script, with its own
`config/settings.toml` (fps cap 0, vsync off, the achievement set explicit: `--user_data_root` reads
the settings there), `--game_data_root` at the app's game folder (this root holds no game),
`--pgo_profile_dir=/data/user/0/<package>/files/pgo` with each of `fight.txt`,
`fight5.txt` and `town.txt`, the `.profraw` files pulled with `run-as ... cat`, merged with the
NDK's `llvm-profdata merge --sparse`. The result goes to the private assets repository as
`pgo/android-arm64.profdata`, with the commit of `generated/`'s codegen, the NDK version and the
date beside it (`pgo/README.md` there).

**A flag-switched optimisation is trained on.** Code that runs only behind a flag is cold to the
compiler when the training ran with the flag off: it is optimised for size and laid out away from
the hot path, and measuring it then measures that, not the optimisation (docs/regressions.md, the
skeleton port). So any new optimisation behind a flag is measured with a profile trained with it on,
and a profile for a build where a flag may be either way is trained with both: every script once
with the flag off and once with it on, in the same order and length, merged into one profile. The
Linux release profile (`pgo/linux-x86_64.profdata`, x86-64-v3) is trained that way for
`--native_skeleton` (on by default there): `fight.txt`, `fight5.txt` and `measure-fight-town.txt`
with `--native_skeleton=false`, then the three again with `--native_skeleton=true`, each from a
fresh bench save, offscreen on the desktop (the first one, 2026-11-01, on the i7-8750H; the Steam
Deck measured with it, it did not train it). The Windows release profile
(`pgo/windows-x86_64.profdata`, `fce8f9b9...`, 2026-11-01) was trained the same way by the desktop
kit (`fight.txt`, `fight5.txt`, `town.txt`, off then on); the port is on by default there too since
2026-11-06. From the GUI redraw filter on (`--native_gui_redraw_filter`, off by default), the "on"
half of every desktop training sets both flags: the three scripts with `--native_skeleton=false
--native_gui_redraw_filter=false`, then the three with `--native_skeleton=true
--native_gui_redraw_filter=true` (the desktop kit's `run_pgo.py` since 2026-11-06; the Linux runs
the same). The profiles in the assets repository predate it: the filter is measured, and any default
for it decided, only with profiles trained this way.

The profiles written before 2026-10-31 summed every minute's copy of the counts (the runtime
appended each write to the file), which weighed the start of each run more: trained again.

### Comparable profiles (an A/B of two builds)

Measuring a change that needs its own profile (a codegen change: every function's hash moves)
compares two builds each with its profile, so the two profiles must be made the same way, or the
comparison measures the training instead of the change. On 2026-10-31 the same base build gave
20.7-21.0 ms in the fight with a profile trained the day before and 22.8 ms with one trained that
afternoon, while the change measured about 1 ms. For any A/B of two PGO builds:
- Both profiles trained the same day, one right after the other, on the same device in the same
  state (battery and temperature as for a measurement, charging the same way).
- The same runs in the same order and of the same length: the bench save copied fresh before each,
  with its own `config/settings.toml` (fps cap 0, vsync off, the achievement set explicit), `fight.txt`, then `fight5.txt`, then `town.txt` (Android) or
  `measure-fight-town.txt` (desktop), each to its script's end, no run cut short or repeated.
- The same writer (`src/pgo/pgo_writer.cpp`: one file per run, replaced every minute) and the
  same merge (`llvm-profdata merge --sparse` of the three files, six for the desktop: the three
  with the flags off, then with them on, as above; nothing else in the folder).
- Each build checked against its own profile (`tools/pgo/check_profile_match.py`, 0 mismatched in
  the generated code), and the profiles' function counts noted next to the result.

## In the release

The profiles live in the private assets repository (the owner's decision), next to `default.xex`, as
`pgo/<platform>.profdata`. Android's APKs are built on our machine until its release job (A6,
docs/android-port.md): those builds configure with `TORCHLIGHT_PGO_USE` pointing at the assets
checkout's `pgo/android-arm64.profdata`. The Linux release job (`release.yml`, `game-linux`) builds
with `pgo/linux-x86_64.profdata` (2026-11-01): it moves the profile out before the assets checkout is
removed, configures with `TORCHLIGHT_PGO_USE`, keeps the build's output in a file and runs the match
check right after the build, before the tests and the packaging. Still to do: the same in Android's
release job (A6). The Windows and macOS release jobs do the same with `pgo/windows-x86_64.profdata` and
`pgo/macos-arm64.profdata` when the assets have them (built without PGO until then), the macOS job
on the pinned Xcode 26.6.

A new or retrained profile goes to a branch of the assets repository first (`pgo/candidate`), and
`release-check.yml` builds the three desktop releases with it: the same builds as the release jobs,
through the same `release` environment and its approval, each one compiled against its profile,
linked with the binding check and measured by the match check, with nothing published or uploaded.
The public log only shows each build's outcome lines (`tools/release/build_outcome.py`). The
profiles go to the assets' main only once all three link. It runs on a pushed tag `v-pgo-check-<n>`
on a commit of develop or main that has the workflow (the environment admits only `v*` tags;
`release.yml` ignores these, from that commit on), or by hand once the file is on main, choosing
such a tag as the ref. Check the commit on the approval screen, and delete the tag after the run.

For when ThinLTO comes back (it is out for now, above): it moves code generation from the compile to
the link's parallel backends: about the same total time (360 s against about 450 s locally), a link
of minutes. Its memory grows with the backends: 4 jobs on the Linux and Windows runners (16 GB), 2
on macOS (7 GB): `-Wl,--thinlto-jobs=N`, `-Wl,/opt:lldltojobs=N`, `-Wl,-mllvm,-threads=N`. Windows
already links with lld-link (CMake's default linker for clang there), which reads bitcode; Apple
keeps ld64, which does ThinLTO itself. The ThinLTO
cache does not help the release, which starts from nothing and uses no caches.


