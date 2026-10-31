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

One profile per platform: the generated code differs between them (`REX_PHYS_HOST_OFFSET` is a
comparison on Windows, macOS arm64 and Android and zero on Linux x86-64), which changes the hash of
nearly every function. Any ReXGlue update or codegen change makes every profile stale at once, so
training again is part of every freeze.

| Platform | Compiler and merge tool | Who trains, where |
| --- | --- | --- |
| Linux x86-64 (also the Steam Deck) | clang 21 (apt.llvm.org, the release's), `llvm-profdata-21` | us, on the owner's desktop |
| Android arm64 | NDK r30 clang, its `llvm-profdata` | us, on the POCO X7 (`files/pgo`, pulled with `run-as`) |
| Windows x86-64 (later) | clang 21.1.8 in GNU mode for the MSVC target (`windows_toolchain.ps1`), `C:\llvm\bin\llvm-profdata.exe` | the owner's Windows PC, an instrumented build we hand over |
| macOS arm64 (later) | AppleClang of the release's Xcode, `xcrun llvm-profdata` of the same Xcode (pinned: AppleClang's profile format does not mix with upstream LLVM's) | the owner's Apple Silicon Mac |

## When to train again

A profile fits one generated code: train again after any codegen or ReXGlue update (new
`generated/`), or a change of the compiler (NDK) that reads it. The generated code's build fails
when its functions no longer match the profile (`-Werror=profile-instr-out-of-date` on
`torchlight_recomp`); the game's own code, which changes every day, only warns and loses the
profile for the functions that changed, so its share of the gain fades until the next training. A
large change of what the game does per frame (a new native path) also calls for a new profile.

Android's training, from a build with `-DTORCHLIGHT_PGO_GENERATE=ON` packaged with
`tools/android/build_apk.sh`: the bench save copied fresh before each script, the settings at fps
cap 0 and vsync off, `--pgo_profile_dir=/data/user/0/<package>/files/pgo` with each of `fight.txt`,
`fight5.txt` and `town.txt`, the `.profraw` files pulled with `run-as ... cat`, merged with the
NDK's `llvm-profdata merge --sparse`. The result goes to the private assets repository as
`pgo/android-arm64.profdata`, with the commit of `generated/`'s codegen, the NDK version and the
date beside it (`pgo/README.md` there).

The profiles written before 2026-10-31 summed every minute's copy of the counts (the runtime
appended each write to the file), which weighed the start of each run more: trained again.

## In the release

The profiles live in the private assets repository (the owner's decision), next to `default.xex`, as
`pgo/<platform>.profdata`. Android's APKs are built on our machine until its release job (A6,
docs/android-port.md): those builds configure with `TORCHLIGHT_PGO_USE` pointing at the assets
checkout's `pgo/android-arm64.profdata`. Still to do, with A6 and for Linux: every Game job of
`release.yml` already checks the assets out; the job is to move its platform's profile out before
the checkout is removed, configure with `TORCHLIGHT_PGO_USE`, keep the build's output in a file and
run the match check right after the build, before the tests and packaging.

For when ThinLTO comes back (it is out for now, above): it moves code generation from the compile to
the link's parallel backends: about the same total time (360 s against about 450 s locally), a link
of minutes. Its memory grows with the backends: 4 jobs on the Linux and Windows runners (16 GB), 2
on macOS (7 GB): `-Wl,--thinlto-jobs=N`, `-Wl,/opt:lldltojobs=N`, `-Wl,-mllvm,-threads=N`. Windows
already links with lld-link (CMake's default linker for clang there), which reads bitcode; Apple
keeps ld64, which does ThinLTO itself. The ThinLTO
cache does not help the release, which starts from nothing and uses no caches.


