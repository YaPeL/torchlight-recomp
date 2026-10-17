# The game thread per frame: desktop data and a POCO X7 5G estimate

For docs/android-port.md, performance section. No new runs: the data comes from logs and documents
that already existed.

## The benchmark this rests on (BENCHMARK.md, second pass)

POCO X7 5G (Cortex-A78 at 2.5 GHz, pinned, no throttling) against the M2, on one core:

| Kernel | POCO / M2 |
|---|---|
| Integer and branches | 1.33× slower |
| Memory reaching DRAM (64–128 MiB) | 1.7–1.9× (about 170 against 100 ns a load) |
| Float n-body (4-lane vector divide and square root) | 3.75×; the same generated code on both, so the A78's vector divide and square root throughput |

## What exists for the game thread

- **No per-frame percentiles of the game thread exist** (p50, p95 and p99), on any machine.
- **The macOS logs** (`frame time (presented ...)` and `frame time (guest swap to swap)`) are frame
  periods under the 60 or 120 fps cap, not the thread's work.
- **The thread's CPU per frame** is measured only as a mean, on Linux:
  - the ThinkPad X1 Extreme, i7-8750H, about 3.0 GHz under its thermal limit;
  - three interleaved scripted runs per side, 2026-10-14 (docs/game-thread-parallelism.md, 12.4;
    docs/performance-profile.md).

| Scene (Linux, i7-8750H) | Game thread CPU per frame, mean | Spread between runs |
|---|---|---|
| Fight | 4.75–4.87 ms | 4.50–4.98 |
| Town square | 5.20–5.37 ms | 5.22–5.57 |
| Mine, still | 4.34–4.51 ms | 4.29–4.80 |

- **The Windows desktop** (Ryzen 7 5700X3D, an older build with series up to 19, uncapped, the main
  thread the limit) gives the walk through town at 98–100 fps, about 10 ms a frame. Walking costs
  about twice the standing town square.
- **The presented maximum** on every machine has hitches of 150–250 ms (streaming, loads); those
  scale with the CPU too.

## POCO X7 5G estimate (×1.5–2 on the desktop means)

| Scene | Desktop | POCO estimate | Budget at 30 fps / 60 fps |
|---|---|---|---|
| Fight | 4.8 ms | 7–10 ms | 33.3 / 16.7 ms |
| Town square, still | 5.3 ms | 8–11 ms | 33.3 / 16.7 ms |
| Town, walking (Windows, older build) | about 10 ms | 15–20 ms | 33.3 / 16.7 ms |

- The factor applies to the i7-8750H laptop, whose one-core speed under its thermal limit is below
  the M2's. Against the laptop the range is therefore on the pessimistic side.
- These are means: no tail is known. The backend thread also needs a big core (the POCO has 4 A78
  cores).
- **Reading:** 30 fps has room everywhere. 60 fps looks reachable standing and fighting, and
  borderline while walking through town.
- **To confirm:** simpleperf on the device, or per-frame thread timings once the game runs there.

## Divide and square root in the recompiled code

- **Static count** over `generated/default`:
  - `fdivs` 972, `fdiv` 119, `fsqrts` 360, `fsqrt` 14;
  - VMX128 reciprocal estimates: `vrefp128` 5, `vrsqrtefp128` 2.
- **Where they are:** they appear in 596 of 43,690 functions (1.4 %).
- **How they are lowered:**
  - The scalar ones are computed in double and rounded to float
    (`double(float(sqrt(x)))`, `double(float(a / b))`), one value at a time.
  - The vector estimates are lowered to a full-precision divide and square root
    (`simde_mm_div_ps(1, simde_mm_sqrt_ps(x))`), which is the slow case on the A78. They sit in 6
    functions (`sub_82781190`, `sub_82780B50`, `sub_82781338`, `sub_8276BD08`, `sub_827809C8`,
    `sub_8269B740`), none of them identified as hot.
- **The hot blocks have none**, following direct calls three levels down:
  - render submission (`renderSingleObject` `0x821C5FA0`, `_setPass` `0x821C5730`, the other render
    targets `0x82198608`);
  - culling (`_findVisibleObjects` `0x821A6070`, the octree walk `0x821C7158`);
  - the bone matrices (`cacheBoneMatrices` `0x821B93B0`, `Entity::_updateAnimation` `0x821B9478`).

  Together these are about 70–80 % of the thread. `Entity::_updateRenderQueue` has 2 `fsqrts` and
  2 `fdivs`; the frustum test is already host code.
- **Most of them sit under the game logic:** the level update `0x821E5910` has 44 `fsqrts` and 58
  `fdivs` (distances and normalised directions); the game logic is 10–18 % of the thread.
- **The most divide-heavy functions** (`sub_825DE980` with 36 `fdivs`, `sub_82465148`,
  `sub_826A0338`, `sub_823C48E8` with 12 `fsqrts`) never showed in the profiles' hot lists.
- **Six small square-root plus divide functions** (21–36 instructions, normalise-like) could be
  called often; the profile can't name them: `sub_82199360`, `sub_821CE140`, `sub_821DBAB0`,
  `sub_8219E9A8`, `sub_8244AB60`, `sub_821AC1E8`.
- **Limit:** indirect (vtable) calls are not followed, so a subtree reached only through them is not
  counted.
- **Conclusion:** no hot spot of divide or square root is known. The guest's float work is mostly
  scalar, so the 3.75× vector kernel overstates it, and the integer ratio (1.33×) is the closer
  guide. A device profile settles it.
