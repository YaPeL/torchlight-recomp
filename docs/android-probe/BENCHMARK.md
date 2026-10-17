# One-core CPU benchmark

`onecore.cpp`, next to this file. Each kernel does a fixed amount of work, and the best of 5 runs
is reported in ms. Not a standard benchmark: only the ratios between machines matter.

- **Android build:** `aarch64-linux-android30-clang++ -O2 -march=armv8.2-a -static-libstdc++
  -Wl,-z,max-page-size=16384` (NDK r30). It was pushed to `/data/local/tmp`, run twice pinned to
  one big core (`taskset 80`, cpu7), then deleted.
- **macOS build:** `clang++ -O2 -std=c++17`; the scheduler picks the core, a performance core in
  practice.

## Results (the second run; the first ran while the clocks ramped up)

| Kernel | M2 | POCO X7 5G (A78, 2.5 GHz) | POCO / M2 | Tab S5e |
|---|---|---|---|---|
| integer (4 MiB state machine, ×40) | 287.6 | 436.1 | 1.52× | pending |
| pointer_chase (16 MiB, 20M dependent loads) | 224.2 | 2678.5 | 11.9× | pending |
| float (64-body n-body, 40000 steps) | 152.3 | 578.9 | 3.80× | pending |

The POCO's first run gave 536.1, 2893.7 and 681.5. The M2's first run gave 288.3, 227.5 and
152.7.

Reading:

- **Pointer chase.** On the M2 most of the 16 MiB working set fits its large L2 (16 MiB on the
  performance cluster). The A78 has 512 KiB of L2 plus a small shared L3, so its number is mostly
  DRAM latency: about 134 ns a load, against about 11 ns. Code that walks large structures will
  feel this most.
- **Float.** Scalar single-precision code with a divide and a square root per inner iteration;
  about 3.8× slower.
- **Integer and branches** are the closest, at 1.5×.

## Thermal state

- **M2:** `pmset -g therm` recorded no thermal or performance warning level.
- **POCO:** battery at 36.0 °C, 77 %, on USB power. The thermal zones are not readable from
  `adb shell`.
