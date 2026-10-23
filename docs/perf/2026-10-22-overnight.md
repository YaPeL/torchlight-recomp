# Overnight performance work, 2026-10-22

The owner's list for the night (CPU first, then the GPU leftovers), worked without the owner. Every
change is on its own branch from `develop` (or from the OGRE patch mechanism's branch where it needs
it); nothing that changes the desktop was integrated. Measurements on the POCO X7 (Mali-G615 MC2)
and on this laptop (i7-8750H; NVIDIA GTX 1050 Ti Max-Q and Intel UHD 630), isolated profiles
(`--user_data_root`), the combat bench (`fight.txt`: 20 s at rest, then 21 enemies and 30 s of
fight), A B B A.

## Summary

| Change | Branch | Integrated | POCO X7: guest p50 fight | POCO X7: GPU wait (present) fight | POCO X7: presented fight / rest | Laptop NVIDIA: presented fight / rest | Laptop Intel: presented fight / rest |
| --- | --- | --- | --- | --- | --- | --- | --- |
| RTSS skinning: rekindled's OGRE patch 0024, with the OGRE patch mechanism | `perf/ogre-skinning-patch` | no (patches/ is a sensitive area; changes the desktop) | 25.3 / 26.1 -> 20.6 / 22.0 ms | 28.1 / 28.5 -> 1.5 / 1.6 ms | 26.1 / 23.6 -> 45.2 / 43.3 fps; 67.8 / 66.5 -> 81.7 / 79.2 fps | 96.8 / 102.3 -> 96.7 / 97.1 fps (none); rest unchanged | 86.9 / 89.7 -> 90.8 / 89.3 fps (+1.9 %, noise); rest unchanged |
| Our capture: constants compared as guest bytes, dense state stamps | `perf/capture-cost` | no (changes the desktop) | 19.95 / 19.72 -> 21.57 / 20.82 ms (worse: build layout, see Method) | unchanged | 47.1 / 47.8 -> 45.1 / 45.1 fps | not measured | not measured |
| "GPU Commands" spin (SDK patch 48, the ReXGlue agent's; its CPU -70 % at cap 0, -35 % at cap 60) | PR #14 (ReXGlue agent) | no (held for the owner's OK) | 21.15 / 19.61 -> 21.41 / 19.79 ms (fps cap 0) | unchanged | 45.0 / 47.3 -> 44.0 / 47.0 fps | not measured | not measured |
| Depth discard on GLES (OGRE patch, lookahead rule) | `perf/gles-discard-depth` | no (tentative) | 20.29 / 20.40 -> 21.38 / 21.61 ms (build layout) | 1.72 / 1.64 -> 1.64 / 1.60 ms | 45.9 / 45.8 -> 44.0 / 43.8 fps | n/a (GLES only) | n/a |
| Sampler state written once per texture (rekindled 0020) | `perf/gles-sampler-once` | no (tentative) | 26.68 ms (5-min fight, one run; base 26.81 / 28.00) | 1.50 ms | 35.8 fps (5-min fight; base 35.7 / 34.1) | n/a (GLES only) | n/a |

Before -> after, both runs of each side in A B B A order. Tentative: the depth discard and the
sampler patch (no gain shown in frames; power in the 5-minute runs). The capture change is kept on
its branch with its measurements: the profile shows our code ~0.15 ms lighter, the A/B cannot
separate that from the build's layout.

## Method, and what tonight showed about it

- **A rebuilt game library moves the guest by about 1 ms on the POCO.** Rebuilding the same commit
  gives a byte-identical library (checked), but any code change, even one that never runs on the
  guest thread (the depth discard: backend and OGRE only), moved the guest's fight p50 by +1.1 to
  +1.3 ms against the unchanged baseline library, with both baseline runs better than both new
  ones. The recompiled guest code is linked after our code; changing our code's size moves every
  guest function, and the guest thread is that sensitive to code placement. A/B results between
  different builds therefore carry a bias of about +-1 ms on the guest; a profile of where the time
  goes (simpleperf) is the better judge for changes smaller than that.
- **Power**: the POCO's battery current (`cmd battery get current_now`) while it runs on USB
  (500 mA): the game draws more than USB gives, so the battery's share follows the consumption.
  Sampled every ~4 s with the big cores' frequency and the CPU temperature (`run-pocop*.sh`, local).
  Short runs are too noisy for it; the 5-minute fights are below.
- The laptop runs were made with the other agents' builds held.

## 1. Our capture (2.9 ms between scene and projectors)

> **Correction (2026-10-22, later the same day).** The POCO X7 profiles of this report were taken
> from the input script's rest mark, 20 s before the fight, not from the fight: the scripts' `adb
> shell grep -q "perf step 1"` lost its quotes on the device and matched "perf" alone, so each
> 25-second "fight" profile held about 18 s of rest (docs/regressions.md, "Profiles of the fight
> taken on the rest mark"). The milliseconds and shares of our capture below do not hold; the
> decision on `perf/capture-cost` came from the same-binary A/B of frames (archived as
> `archive/capture-lean`), which the error did not touch. Not repeated (the owner's decision).

Profile (POCO X7, fight, simpleperf 25 s, per sample: the first frame of ours from the leaf, so a
hook that runs the game's code inside it is not counted as ours): our code is **4.15 ms a frame**
(18.5 % of the guest thread), of which capture 2.9 ms, the native culling hooks (`BoxExtentDecides`,
`PlanesAdmit`, bucket lookups) ~0.6 ms, the guest copy hook (the game's own memcpy, done natively)
0.27 ms. The largest: `ReadConstants` 0.46 ms (half of it copying and byte-swapping every value
before asking whether it changed), `Session::State` 0.28, `Session::Unchanged` 0.27 (a cache miss
into the shadow map's nodes per state hook), `RecordContent` 0.21, `ReadVertexBufferBinding` 0.17.

Done (`perf/capture-cost`, reviewed in a clean context; its finding fixed: the guest word helpers
moved to `guest_abi` with a test of the host mapping's seam):
- constant ranges compared as the guest's bytes and swapped only when sent;
- the state early outs check a dense array of stamps instead of the map's nodes.

Result: in the profile, `ReadConstants` 2.04 % -> 1.54 % and `Unchanged` 1.20 % -> 0.97 % of the
guest thread (~0.15 ms). The A/B (K Q Q K) gave the new build +1.3 ms, which the profile does not
show anywhere in our code (Method). Validation: unit tests (109 pass); replays do not run the
capture code, so image checks do not apply. Not integrated (changes the desktop); the rest of our
cost is spread thin (no other function over 0.3 ms), so two hypotheses did not pay and I moved on.

## 2. The "GPU Commands" thread

What it does in native mode: the null GPU plugin (`patches/rexglue-gpu-null-plugin.patch`) draws
and copies nothing, but keeps the guest's GPU protocol alive: ring buffer, fences, event writes,
interrupts, vblank. The guest waits on that work, so it cannot be switched off; nothing of it is
read by our frontend (no read-backs, no EDRAM resolves: those are no-ops). Its cost was spinning:
`sched_yield` 49 % and `clock_gettime` 28 % of its samples, in `WAIT_REG_MEM` (which yields instead
of sleeping when the runtime's vsync cvar is off, i.e. fps cap 0: `src/live/install.cpp:327`) and
in the idle loop (500 yields before each event wait).

The SDK is the ReXGlue agent's: patch 48 (CP spin backoff) is its change, PR #14, built into
a separate Android SDK install; the A/B is against the current install, same game commit.

> **Correction (2026-10-22, later the same day).** The POCO X7 profiles of this report were taken
> from the input script's rest mark, 20 s before the fight, not from the fight: the scripts' `adb
> shell grep -q "perf step 1"` lost its quotes on the device and matched "perf" alone, so each
> 25-second "fight" profile held about 18 s of rest (docs/regressions.md, "Profiles of the fight
> taken on the rest mark"). The "GPU Commands" shares below are rest and fight mixed; the frame
> times, presented rates and battery currents come from the runs' logs and hold. Not repeated (the
> owner's decision: patch 48 was decided on the frames).

| fps cap 0 | guest p50 / p95 | presented | GPU Commands, share of the app's CPU cycles |
| --- | --- | --- | --- |
| base | 21.15 / 26.17, 19.61 / 25.75 ms | 45.0, 47.3 fps | 9.49 % |
| patch 48 | 21.41 / 27.24, 19.79 / 26.31 ms | 44.0, 47.0 fps | 2.83 % |

| fps cap 60, vsync (normal play) | guest p50 / p95 | presented | GPU Commands share | battery current |
| --- | --- | --- | --- | --- |
| base | 20.52 / 26.31, 18.62 / 25.61 ms | 45.5, 49.1 fps | 3.42 % | -276, -319 mA |
| patch 48 | 20.07 / 25.75, 21.30 / 27.70 ms | 46.1, 44.1 fps | 2.24 % | -446, -451 mA |

Every run started at thermal status 0, battery 30.0 C, CPU 34.7-36.3 C, skin 32.4-32.5 C; during
the fights the big cores stayed at 2500 MHz and the CPU at 53-57 C (max 55-59 C). The short runs'
battery current is noise (the 5-minute fights below disagree with it). The A/B was sent to the
ReXGlue agent, who put the numbers in patch 48's header; I reviewed patch 48 (0 Important) and the
merge is held for the owner's OK because it also changes the desktop SDK.

### Five-minute fights (fps cap 60, vsync)

`fight5.txt` (local): the bench's start, then ten waves of its 21 enemies, 30 s of fight each, the
level cleared between waves. Start state, then fight guest p50 / p95, presented, and during the
fight (sampled every ~4 s after 60 s) the battery current, the big cores' frequency and the CPU
temperature:

| Run | Start (status, battery, CPU, skin) | guest p50 / p95 | presented | battery current | big cores | CPU mean / max |
| --- | --- | --- | --- | --- | --- | --- |
| base (K) | 0, 30.0, 34.8, 32.4 C | 26.81 / 37.85 ms | 35.7 fps | -474 mA | 2500 MHz | 56.9 / 61 C |
| patch 48 (C2) | 0, 31.0, 36.5, 33.2 C | 28.10 / 41.56 ms | 33.9 fps | -375 mA | 2500 MHz | 55.5 / 58 C |
| patch 48 (C2) | 0, 31.0, 37.5, 33.6 C | 26.38 / 38.74 ms | 36.1 fps | -452 mA | 2500 MHz | 57.7 / 60 C |
| base (K) | 0, 32.0, 36.6, 33.6 C | 28.00 / 40.09 ms | 34.1 fps | -417 mA | 2500 MHz | 57.7 / 60 C |
| depth discard (Z2) | 0, 32.0, 37.7, 33.7 C | 25.38 / 38.11 ms | 37.4 fps | -422 mA | 2500 MHz | 58.7 / 62 C |
| sampler once (S2) | 0, 32.0, 38.3, 33.8 C | 26.68 / 38.17 ms | 35.8 fps | -407 mA | 2500 MHz | 58.2 / 66 C |

The cool-down (battery at or below 30 C, at most 6.4 minutes) did not always reach 30 C between
these long runs. Patch 48: -414 against -445 mA on average, inside the runs' spread (-375 to
-474); no change in frame times or temperature; the big cores never left 2500 MHz. The depth
discard and the sampler patch, one run each, fall inside the base's spread too: no measurable
power or frame effect.

## 3. The projector passes and the scene: plan

> **Correction (2026-10-22, later the same day).** The POCO X7 profiles of this report were taken
> from the input script's rest mark, 20 s before the fight, not from the fight: the scripts' `adb
> shell grep -q "perf step 1"` lost its quotes on the device and matched "perf" alone, so each
> 25-second "fight" profile held about 18 s of rest (docs/regressions.md, "Profiles of the fight
> taken on the rest mark"). The table below (projector passes and the main scene) does not hold.
> Taken again with the fixed scripts on 2026-10-23, for `fight.txt` and `fight5`'s waves 1 and 10:
> docs/perf/native-render-queue.md, "What was measured". Its "firePreFindVisibleObjects" row was also misnamed:
> `0x821BC5A0` holds the render queue walk too, and the part of it that grows is the game's
> render queue listener drawing the GUI (`fireRenderQueueStarted` 0x821BC468), not a scene graph
> update; the estimates below that rest on these rows (the walk, pass setup) are superseded there.

Measured (POCO X7, combat bench, the skinning patch in, simpleperf 25 s, guest thread 22.4 ms a
frame; ms per frame, each sample counted once, particles first):

| Part | Projectors (2 passes) | Main scene |
| --- | --- | --- |
| Culling: Runic's octree walk (`0x821A6070` -> `0x821C7158`) | 1.43 | 1.25 |
| OGRE's draw path (render queue walk `0x821DD098`, `renderSingleObject` `0x821C5FA0`, `_setPass` `0x821C5730`) | 1.85 | 3.55 |
| Our capture (hooks turning those calls into commands) | 1.02 | 1.89 |
| Pass setup inside `_renderScene` (`0x821B07F0`), other than the above | 0.83 | 1.63 |
| of it: `firePreFindVisibleObjects` (vtable slot 18 of the scene manager, `0x821BC5A0`; the slot read by position, a hypothesis) | 0.03 | 0.95 |
| Controllers (`0x821C8C00`, once a frame, paid by the first pass: the shadow projector) | 1.35 (particles included) | |

rekindled (its main thread, same fight kind): ~1.8 ms for both projector passes, ~1.0 ms culling
and ~3.5 ms draw submission for the main scene (split by draw counts, an estimate).

What can go native without calling game code in the middle:

1. **Octant and node tests of the octree walk** (`0x821C7158`). Pure: loose octant bounds against
   the camera (`OctreeCamera::getVisibility`, `0x821C0E78`), node boxes against six planes
   (`Frustum::isVisible`, `0x821C74A0`), an explicit stack. The objects of a visible node still go
   through the guest (`isVisible`, `_notifyCurrentCamera`, `_updateRenderQueue` are virtuals of
   game and OGRE classes). A native walk that calls back into the guest only per visible node's
   objects keeps every virtual call and its order. Estimate: the walk's own share, ~60 % of the
   culling rows (the rest is the per-object virtuals): ~0.8 ms projectors + ~0.7 ms main, less the
   native cost (~0.2 ms): **~1.3 ms**. Risk: medium (the float tests must give the guest's answers
   bit for bit: `hooks/frustum_test.cpp` already does the six-plane test that way for buckets).
2. **Pass setup that repeats per pass.** `_updateSceneGraph` walks the whole node tree in each of
   the three passes; between passes only the projector cameras move (Runic's projector setup,
   `0x82210148`, and the listener `0x821E9C50`). Skipping a walk is safe only when no node is dirty;
   OGRE 1.7 keeps the dirty flags on the nodes, so a native check of "any node marked since the last
   walk" needs the node queue (`Node::queueNeedUpdate`) read from the guest. Estimate: up to
   ~0.6 ms of the projectors' 0.83. Risk: medium-high (listeners on nodes; Runic's own nodes).
3. **Not native**: the controllers (1.35 ms: their sources, functions and destinations are virtuals
   into ParticleUniverse and game classes), `firePreFindVisibleObjects` (0.95 ms: game listeners),
   the render queue walk and `renderSingleObject` (they call the guest's RenderSystem virtuals,
   which we hook, and game `Renderable` virtuals per object: world transforms, custom parameters).
   The large win there is a different design: a native renderer of the render queue that reads the
   guest's queued renderables and passes and emits our commands directly, replacing both OGRE's draw
   path and our capture (~1.85 + 3.55 + 1.02 + 1.89 = **~8.3 ms** today). It must reproduce
   `_setPass`, the auto parameters and every Runic difference: a project of its own, not a step.

Reuse between passes: the shadow and light passes draw different object sets (casters only;
light quads and static geometry), so their render queues share nothing with the main scene's.
What repeats is the scene graph walk (item 2) and, inside the walk, the octree's node boxes
(`OctreeNode::_updateBounds`, `0x821BE630`), recomputed per pass; they could be computed once a
frame when no node moved in between (the same dirty check as item 2).

How to verify it is identical (for 1 and 2): a verification mode (a command-line option, off by
default) that runs the native version and the guest's original on the same frame and compares
their outputs (the list of objects queued, in order, per pass; for 2 the derived transforms of
every node) and logs the first difference; plus replays of F9 captures taken with the native path
against the same frames with it off, byte for byte. Only after a session of fights and towns with no
difference does the native path become the default.

Not prototyped tonight: none of the three is small enough to finish and verify in one night
without risking the game's behaviour; item 1 is the first candidate.


## 4. Depth discard (GLES only)

The rule, from the guest's code (`src/guest_abi/ogre_layout.h`, `render_system`): Runic's
`_setRenderTarget` binds one depth surface for every target (a render texture binds only its
colour; only an anti-aliased window binds a depth of its own), and every pass starts with its
viewport's clear. So when the pass a bind starts clears the whole depth before it draws, and its
target is at least as large as the one left, the left target's depth is never seen again. The
frontend plans that per frame (`frontend/depth_discard.h`, tested), the backend discards through
`RenderTexture::swapBuffers`, and `patches/ogre-gles2-discard-depth-on-swap.patch` (ours) makes a
GLES 3 render texture's swap invalidate its depth and stencil. Every backend target has a depth
pool of its own (OGRE's default pool shared one buffer between a 512x512 texture and the main
target, the review's finding). The main target is not discarded (the next frame is not known when
a frame ends).

Live: 5 `glInvalidateFramebuffer` a frame (shadow, light and output targets, and the main target
left for the shadow pass in the build measured, before the review's fix). Images: 30 captures on
the POCO, 150 PNGs, identical; GL3+ (no-op there), 12 captures identical. A/B: no change in the
GPU wait (1.6-1.7 ms either way: with the skinning fixed the GPU is not the limit), the guest's
+1.1 ms is the build layout (Method). Tentative: its effect, if any, is power (below).

## 5. The framebuffer changes' CPU cost on the backend thread (~1.45 ms)

> **Correction (2026-10-22, later the same day).** The POCO X7 profiles of this report were taken
> from the input script's rest mark, 20 s before the fight, not from the fight: the scripts' `adb
> shell grep -q "perf step 1"` lost its quotes on the device and matched "perf" alone, so each
> 25-second "fight" profile held about 18 s of rest (docs/regressions.md, "Profiles of the fight
> taken on the rest mark"). The backend thread's CPU figure in this section (~1.45 ms) came from
> those profiles and does not hold; the counts of binds and changes come from the GLES layer's
> counters and hold. Not repeated.

Per fight frame (GLES layer, live): ~17 binds, ~9.5 changes. OGRE 14's GLES2 `_setViewport` calls
`_setRenderTarget`, which binds the FBO again every time (no cache), with a `dynamic_cast` and a
depth buffer check, for any viewport marked updated, even on the same target. Of the changes, five
are needed (shadow, light, main, output, window) and one is not: `tl_backend_begin` binds the main
target before the guest binds its first pass (OGRE needs a viewport before `_beginFrame`). The
other ~8 binds are the same FBO again (viewport changes and clears). Avoiding them needs an OGRE
patch that skips binding the active target again (and invalidates that on every path that binds
another FBO: read-backs, uploads, our discard), or deferring `_beginFrame`. Estimate 0.2-0.4 ms of
the backend thread, which is not the limit (~7 ms against the guest's ~22). Not prototyped.

## 6. Texture binds per draw (3-4 against rekindled's ~1)

> **Correction (2026-10-22, later the same day).** The POCO X7 profiles of this report were taken
> from the input script's rest mark, 20 s before the fight, not from the fight: the scripts' `adb
> shell grep -q "perf step 1"` lost its quotes on the device and matched "perf" alone, so each
> 25-second "fight" profile held about 18 s of rest (docs/regressions.md, "Profiles of the fight
> taken on the rest mark"). The share of `_setSampler` below (2.3 % of the backend thread) came from
> those profiles and does not hold. The patch was decided later on a same-binary A/B (backend render
> about -0.7 ms on the POCO X7) and is in `develop`.

Why: OGRE 14's GLES2 `_setTexture` calls `glBindTexture` every time (no per-unit cache), our
backend's `_disableTextureUnitsFrom` per draw unbinds the units above the draw's (which another
draw then binds again), and every bind rewrites all the sampler parameters (`glTexParameter`).
Taken: rekindled's patch 0020 (sampler state written once per texture, keyed on the texture
object) with the two lines of its 0018 that reset the key when the GL texture is made or freed,
Switch guards removed (`perf/gles-sampler-once`, credited in the patch, the commit and
`THIRD_PARTY_NOTICES.md`). It addresses `_setSampler`, 2.3 % of the backend thread (~0.24 ms).
Validation: 30 captures replayed on the POCO with it, 150 PNGs identical to the base; the review
(clean context) found no Important issue (its two nits fixed). Effect: one 5-minute fight inside
the base's spread (below); the backend thread is not the limit, so a frame gain is not expected.
Tentative.

Found on the way: rekindled's 0018 also fixes an OGRE GLES2 bug we likely share:
`GL_TEXTURE_MAX_LEVEL` is set to `mNumMipmaps + 1`, a level that does not exist, so a texture whose
mip chain stops short of 1x1 is incomplete and samples black (on the Switch: the characters' rim
light, `rimlight.dds`, and four particle textures). Not taken tonight: it changes images, so it
needs its own check against Xenos frames.

## 7. Laptop

Only the skinning patch has a desktop effect to measure (the others are GLES-only or showed nothing
on the POCO). Horde (fight.txt), 1280x720, no cap, no vsync, guest mean / backend render / GPU wait:

| | NVIDIA A | NVIDIA K | Intel A | Intel K |
| --- | --- | --- | --- | --- |
| fight presented | 96.8, 102.3 fps | 96.7, 97.1 fps | 86.9, 89.7 fps | 90.8, 89.3 fps |
| rest presented | 174.1, 168.6 fps | 171.0, 174.8 fps | 151.5, 148.9 fps | 149.7, 153.8 fps |
| guest mean, fight | 10.31, 9.76 ms | 10.33, 10.27 ms | 11.50, 11.13 ms | 10.99, 11.19 ms |
| backend render / GPU wait, fight | 3.92 / 3.35, 3.66 / 3.36 ms | 3.91 / 3.47, 3.97 / 3.51 ms | 3.61 / 0.46, 3.61 / 0.45 ms | 3.48 / 0.48, 3.66 / 0.44 ms |

No change on the laptop: its GPUs do not copy the bone array the way Mali's compiler does, and the
guest thread limits there. One Intel run of K hung at start (the guest waiting on the GPU from 10 s
after launch, before any level, until run_capped closed it); the repeat ran normally. Noted, not
reproduced.

## Decisions taken without the owner

- Desktop image checks run under Xvfb (llvmpipe) on 12 captures, not on the NVIDIA screen.
- The capture change kept, not reverted, though its A/B is worse: the profile shows it lighter and
  the A/B's difference is the build layout; marked as no measured gain.
- The SDK patch was left to the ReXGlue agent (its series; the owner unblocked its permission).
- rekindled's 0018 mip-level fix not taken (it changes images).
- No prototype for item 3 (none of its parts is small enough to verify in one night).

## Waiting for the owner's OK

Nothing was integrated tonight: every change either touches the desktop (the OGRE patch mechanism
and the skinning patch change the desktop's shaders; the capture code and the depth discard's
frontend and backend code run there too) or depends on one that does (the sampler patch needs the
mechanism).

1. **`perf/ogre-skinning-patch`** (rekindled's 0024 + the OGRE patch mechanism): the night's real
   gain on the POCO (fight ~25 -> ~44 fps presented, GPU wait 28 -> 1.5 ms), no change on the
   laptop. Clean-context review: its Important finding (Windows PowerShell 5.1 stopping on the
   patch check) fixed; CI green on Linux, Windows, macOS and Android. `patches/` is a sensitive
   area: the evidence is above.
2. **SDK patch 48** (the ReXGlue agent's PR #14): the "GPU Commands" thread's CPU -70 % (fps cap 0)
   and -35 % (cap 60), nothing else changes. Held by the ReXGlue agent for the owner's OK
   (desktop SDK too).
3. **`perf/gles-sampler-once`** (rekindled's 0020): Android only, needs 1 first. Tentative (no
   measurable effect), images identical, review clean.
4. **`perf/gles-discard-depth`**: Android only in effect, needs 1 first; its frontend/backend code
   runs on the desktop as a no-op. Tentative (no measurable effect), images identical, review's
   Important finding fixed.
5. **`perf/capture-cost`**: no measured gain (the profile shows ~0.15 ms of our code less; the A/B
   cannot see it). Kept for the record; integrating it is optional.
6. **rekindled's 0018 mip-level fix** (not taken): likely fixes black textures on Android (rim
   light, some particles); needs its own image check against Xenos frames first.
7. **Item 3's first step** (native octree walk with a verification mode): the plan above, ~1.3 ms
   estimated.
