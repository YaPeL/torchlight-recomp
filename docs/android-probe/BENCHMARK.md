# One-core CPU benchmark

The fairer second pass (`onecore2.cpp`) comes first. The first pass (`onecore.cpp`, below it) is
kept for the record.

## Second pass (2026-10-17): warmed up, pinned, pointer chase at 16, 64 and 128 MiB

What changed from the first pass:

- **Warm-up:** `onecore2.cpp` runs each kernel 3 times unmeasured, then reports the best of 5.
- **Pointer chase:** at 16, 64 and 128 MiB, the same 20M dependent loads each.
- **POCO X7 5G:** pinned with `taskset 80` (cpu7, a Cortex-A78, part 0xd41). A sampler read cpu7's
  `scaling_cur_freq` every 0.5 s during the whole run: **2.5 GHz in 148 of 149 samples** (one
  2.0 GHz sample at the start), so there was no throttling.
  - CPU sensor (`dumpsys thermalservice`): 31.5 °C a few minutes before the run, 61.5 °C right
    after it. Battery 29.0 °C, on USB power.
  - The binary and the sample file were deleted afterwards.
- **M2:** macOS has no CPU pinning. At the default QoS the scheduler runs it on a performance core.
  - The clock can't be read without `sudo powermetrics`, which was not used; the P cores' maximum
    is about 3.5 GHz.
  - `pmset -g therm` recorded no thermal or performance warning, before or after. The load average
    was 1.85.
  - Caches (`sysctl`): 16 MiB L2 on the 4 P cores, 4 MiB on the E cores.

| Kernel | M2 | POCO X7 5G (A78, 2.5 GHz) | POCO / M2 | Tab S5e |
|---|---|---|---|---|
| integer (4 MiB state machine, ×40) | 288.5 | 385.0 | 1.33× | pending |
| float (64-body n-body, 40000 steps) | 153.2 | 574.8 | 3.75× | pending |
| pointer_chase 16 MiB | 198.8 | 2424.3 | 12.2× | pending |
| pointer_chase 64 MiB | 1605.9 | 2976.6 | 1.85× | pending |
| pointer_chase 128 MiB | 2016.6 | 3446.4 | 1.71× | pending |

M2 with `-march=armv8.2-a`, the same as the Android build: integer 289.1, float 153.2, chase
214.8, 1603.1 and 2017.5. The same within noise.

Reading:

- **Memory.** At 16 MiB the M2 stays in its L2 (about 10 ns a load). At 64 and 128 MiB both machines
  reach DRAM, and the gap is **1.7–1.9×** (about 100 against 170 ns a load). So 12× was cache
  against RAM, not CPU against CPU.
- **Float stays at 3.75×, and it is not the compiler.** Both builds vectorize the kernel the same
  way (`llvm-objdump` and `otool` on `FloatKernel`):
  - 4-lane `fsub`, `fmul`, `fmla`, `fdiv.4s` and `fsqrt.4s`;
  - the force sums added in order as scalars (no `-ffast-math`, so no reassociation).

  The only difference is that Apple clang unrolls the loop ×2. `-march=armv8.2-a` on the Mac
  changes neither the code nor the time.

  What is left is the clock (3.5 against 2.5 GHz, 1.4×) and the per-clock throughput, about 2.7×.
  The kernel is bound by vector divide and square root, and by the in-order add chain; the A78's
  vector divide and square root have far lower throughput than the M2's.
- **Integer and branches:** 1.33×, close to the clock ratio.

## First pass (2026-10-17, `onecore.cpp`)

`onecore.cpp`, next to this file. Each kernel does a fixed amount of work, and the best of 5 runs
is reported in ms. Not a standard benchmark: only the ratios between machines matter.

- **Android build:** `aarch64-linux-android30-clang++ -O2 -march=armv8.2-a -static-libstdc++
  -Wl,-z,max-page-size=16384` (NDK r30). It was pushed to `/data/local/tmp`, run twice pinned to
  one big core (`taskset 80`, cpu7), then deleted.
- **macOS build:** `clang++ -O2 -std=c++17`; the scheduler picks the core, a performance core in
  practice.

### Results (the second run; the first ran while the clocks ramped up)

| Kernel | M2 | POCO X7 5G (A78, 2.5 GHz) | POCO / M2 | Tab S5e |
|---|---|---|---|---|
| integer (4 MiB state machine, ×40) | 287.6 | 436.1 | 1.52× | pending |
| pointer_chase (16 MiB, 20M dependent loads) | 224.2 | 2678.5 | 11.9× | pending |
| float (64-body n-body, 40000 steps) | 152.3 | 578.9 | 3.80× | pending |

The POCO's first run gave 536.1, 2893.7 and 681.5. The M2's first run gave 288.3, 227.5 and
152.7.

Reading (superseded by the second pass):

- **Pointer chase.** On the M2 most of the 16 MiB working set fits its large L2 (16 MiB on the
  performance cluster). The A78 has 512 KiB of L2 plus a small shared L3, so its number is mostly
  DRAM latency: about 134 ns a load, against about 11 ns. Code that walks large structures will
  feel this most.
- **Float.** Scalar single-precision code with a divide and a square root per inner iteration;
  about 3.8× slower.
- **Integer and branches** are the closest, at 1.5×.

### Thermal state

- **M2:** `pmset -g therm` recorded no thermal or performance warning level.
- **POCO:** battery at 36.0 °C, 77 %, on USB power. The thermal zones are not readable from
  `adb shell`.
