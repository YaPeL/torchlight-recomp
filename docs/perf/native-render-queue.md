# Native render queue: plan

Status: plan, approved as a project of its own by the owner (2026-10-22); no code yet. It replaces
proposals 2 (cheaper per-draw capture) and 3 (the constants path) of the same day.

## Why

Measured on the POCO X7 on 2026-10-23 with simpleperf and our symbols, binary 47a1d12 (in
`develop`; the object census in the build, off in `fight.txt`, on in `fight5`), fps cap 0. Each
profile's window was checked against the fight in the run's log (`tools/profile_window`): the
profiles of 2026-10-22 had been taken partly at rest (docs/regressions.md, "Profiles of the fight
taken on the rest mark") and are replaced by these. Milliseconds are the share of the guest thread's
samples times its mean frame in the window.

On the POCO X7 the game's thread is the limit. Walking OGRE's render queue costs 8.2 ms of its 22.1
ms frame in the 30-second fight (`fight.txt`, 25 s profiled from 2.6 s after the mark, about 300
draws a frame by the frame counter, the GUI's included; the stage 0 census counts 270 in the queue):
about 27 us a draw, 17 of them in the guest's embedded OGRE (passes, automatic parameters,
constants, texture units, the per-object work) and 10 in our hooks and capture of each D3D9 call
(`ReadConstants`, the session's state checks, the resource registry, vertex buffer reads). In the
5-minute fight (`fight5.txt`, 11 s profiled in the middle of wave 1 and at the end of wave 10) the
same walk is 9.3 ms of a 25.2 ms frame in wave 1 and 13.2 ms of 39.5 ms in wave 10. The dead and the
loot are never removed in that fight, so the draws grow from about 300 to about 689 a frame by wave
10: about 19 us a draw there. (The draws a frame are the frame counter's, from the twelve `fight5`
runs of 2026-10-22 on binary 03ab9ae, tag `archive/diag-combined`; their wave 10 averaged 34.8 ms a
frame, without the census, which these profiles had on.)

| Inside the walk (ms a frame) | Guest | Ours | `fight.txt` | `fight5` wave 1 | `fight5` wave 10 |
| --- | --- | --- | --- | --- | --- |
| Automatic parameters (`_updateAutoParams` 0x82201EB0) | 0.81 | 0.05 | 0.86 | 0.99 | 1.33 |
| Constants upload (`bindGpuProgramParameters` 0x821C2118) | 0.36 | 0.77 | 1.13 | 1.42 | 1.72 |
| Texture units (`_setTextureUnitSettings` 0x821C93A0) | 0.63 | 0.35 | 0.97 | 1.00 | 1.34 |
| Vertex buffers (`setVertexBufferBinding` 0x821C3E78) | 0.23 | 0.30 | 0.53 | 0.56 | 0.85 |
| Before the draw (`_render` guard 0x821C4058 and `_render` 0x821CEA60: our draw capture) | 0.50 | 0.89 | 1.40 | 1.66 | 2.34 |
| RTSS per object (`notifyRenderSingleObject` 0x821C4F50) | 0.40 | 0.21 | 0.61 | 0.79 | 1.03 |
| Rest of the pass (`_setPass` 0x821C5730) | 0.69 | 0.28 | 0.98 | 1.10 | 1.36 |
| Rest per object (`renderSingleObject` 0x821C5FA0) | 1.14 | 0.11 | 1.25 | 1.37 | 2.37 |
| Rest of the walk (0x821DD098) | 0.47 | 0 | 0.47 | 0.46 | 0.80 |
| **Total** | **5.23** | **2.96** | **8.19** | **9.33** (5.90 / 3.43) | **13.15** (8.50 / 4.65) |

(Guest and Ours are `fight.txt`'s; the waves' split is in brackets on the total row. A sample is
ours when the innermost frame that is either guest code or ours is ours: a hook that only counts
calls and then runs the guest's function leaves the guest's work to the guest.)

Where the walk sits in the frame (ms a frame, the projector passes apart from the main scene;
each sample counted once, in this order: controllers, GUI, particles, culling, draw path; guest
and ours split as in the table above):

| Part | `fight.txt` projectors / main | `fight5` wave 1 | `fight5` wave 10 |
| --- | --- | --- | --- |
| Culling: the octree walk (`0x821A6070` -> `0x821C7158`) | 1.09 / 1.84 | 1.13 / 1.83 | 1.57 / 2.94 |
| OGRE's draw path (`0x821DD098`, `0x821C5FA0`, `0x821C5730`), guest code | 1.39 / 3.80 | 1.50 / 4.35 | 1.37 / 7.06 |
| Our capture under it | 0.78 / 2.17 | 0.98 / 2.43 | 0.70 / 3.92 |
| Pass setup inside `_renderScene` 0x821B07F0, other | 0.67 / 0.37 | 0.76 / 0.34 | 0.93 / 0.41 |
| GUI: the game's render queue listener (below) | - / 1.56 | - / 1.81 | - / 6.01 |
| Particles under the passes | 0.41 / 0.57 | 0.44 / 0.71 | 0.35 / 1.59 |
| Controllers (`0x821C8C00`, paid in the first pass, the shadow projector) | 1.62 / - | 2.05 / - | 3.69 / - |

**Outside the cut: the GUI.** The part that grows most from wave 1 to wave 10 is not the walk but
the GUI, 1.8 -> 6.0 ms. `0x821BC468` is `fireRenderQueueStarted`: it loops over the scene
manager's render queue listeners (the vector at +0x42B8/+0x42BC) and calls each one's slot 3,
`renderQueueStarted(id, invocation, skip)`. The game's listener `0x821FBC60` draws CEGUI there:
the window tree (`0x821AFEA0`, recursive; the windows' own drawing `0x821AFF88` and
`0x821B01B8`) and the GUI renderer's draws (`0x821D2F80`, straight to `_render`). Its draws do not
go through the render queue, so this plan does not touch them; why the GUI grows with the dead
and the loot is still open. The report of 2026-10-22 had named this row "firePreFindVisibleObjects"
and "the scene graph update"; both were wrong (`0x821BC5A0`, which it was named after, holds the
render queue walk too).

The idea: the guest keeps walking its render queue and setting every pass with its own
`_setPass`; for each object of a pass the native path handles, it emits that object's commands
directly (its automatic constants, its buffers, its draw) instead of letting the guest's
`renderSingleObject` turn the object into D3D9 calls that our hooks then turn back into
commands. The commands are the same ones the capture produces today (`commands/types.h`), so the
backend, the frontend and the replay do not change, and the two paths can be compared draw by
draw.

The 2026-10-13 study rated this "F2, native pass renderer" as high risk and XL
(docs/game-thread-parallelism.md). The risk is still there; this plan contains it by going in
stages, each with a fallback to the current path for whatever it does not handle and a
verification mode. An earlier version of this plan also took over the walk and the passes; seven
review rounds kept finding holes in the boundary between native passes and guest passes (the
guest's state caches), and the owner chose on 2026-10-23 to keep every pass the guest's.

## 1. Where to cut

### What the guest keeps

Everything except the per-object work stays the guest's, unchanged:
- the game's logic and the scene graph (`_updateSceneGraph`), the animation (including
  `Entity::cacheBoneMatrices` 0x821B93B0), the controllers (0x821C8C00) and the particle systems;
- culling (`_findVisibleObjects`: OctreeSceneManager vtable 0x82005B9C slot 122, octree walk
  0x821C7158) and each object's `_updateRenderQueue`, which fills the render queue;
- the render target and viewport setup of `_renderScene` 0x821B07F0, which our hooks capture;
- **the whole walk of the queue and every pass:** `renderBasicQueueGroupObjects`, the collection
  sorts, `renderObjects`, `acceptVisitor`, Runic's grouped walk, the visitor and its validations,
  and every `_setPass` (below, "The walk the guest keeps").

### The cut point: inside `renderSingleObject`, one part at a time

`SceneManager::renderSingleObject(renderable, pass, scissoring, autoLights, manualLightList)`
(slot 21, `0x821C5FA0`) is called by the visitor for each object, after the guest's `_setPass` for
its pass. For an object it handles, the native path runs that function's sequence itself, and the
rule that keeps the boundary empty is: **every step that changes guest state is the guest's own
function, called as the guest calls it; the native path replaces only parts whose effect on guest
state it reproduces exactly**, and those parts are taken over one per stage (section 2). So after a
native object the guest's SceneManager, AutoParamDataSource, render system and buffers are as they
would be after the guest's own `renderSingleObject`, whatever comes next (another native object, a
guest object, a pass visit, the GUI), and an object can fall back per object without a boundary in
either direction.

The parts, in the order the stages take them, with what each must leave exactly as the guest does
(read from the recompiled functions):
- **The constants** (`updateGpuProgramParameters`, slot 41, `0x821C26D0`: `_updateAutoParams`
  0x82201EB0 and the upload through render system slot 91 for the ranges `mGpuParamsDirty`
  (+0x43B4, u16) selects, then the mask cleared @0x821C2858). The native path computes the same
  automatic constants for the same mask, the global and manual ranges included when the mask has
  them (the first object after a pass visit, or after `resetViewProjMode` marks them), sends them
  through the session and the constant mirror as the hook's `bindGpuProgramParameters` would, and
  clears the mask. It does not write the guest's GpuProgramParameters buffers: every later guest
  upload is preceded by `_updateAutoParams` for its own mask, which recomputes what it uploads.
- **The RTSS per-object listener** (`fireRenderSingleObject`, slot 13, `0x821C4EE0`, and the
  sub-render states' `updateGpuProgramsParams`): stage 0 reads each supported state for "last
  value" caches and for the guest buffers it writes; the native version writes the same values to
  the same guest parameters, so those caches stay true.
- **The draw and its capture** (the `_render` guard 0x821C4058 and the base `_render` 0x821CEA60):
  the declaration and buffer bindings with the uploads behind slots 84 and 85 (vertex buffers to
  their device copy, 0x821A71E8) and the index buffer uploads (0x82581D98, 0x82582520, 0x82582668)
  that the capture's snapshots read, the dirty clip planes (RS +0x2F4, flushed through slot 124 and
  cleared), the counters (RS +0x84/+0x88/+0x8C), the pass iteration loop (RS +0x298). Until this
  part is native, the native path calls the guest's guard, through our hook, for each draw.
- **The rest of `renderSingleObject`**: the light list and its hash (the guest's
  `useLightsGpuProgram`, slot 39, `0x821C12C0`, which sets the AutoParamDataSource's light list,
  the hash +0x43B0 and the lights dirty bit; an empty list hashes to 0, so the hash is never just
  reset), the per-object render system calls (cull mode restored when the pass's flip flag +0x419F
  is set, against RS +0x74; the pass iteration count, slot 112; the derived depth bias, slot 113;
  the polygon mode with the camera's override, slot 81; `_setTextureUnitSettings` again for units
  with view-relative texture coordinate generation), the camera-relative offset (+0x4358, +0x439C),
  the identity view or projection and `resetViewProjMode` (slot 8), the renderable's
  `preRender`/`postRender`. Until this part is native, the native path calls these guest functions
  and render system setters itself, through our hooks, in the guest's order. `setNormaliseNormals`
  (slot 86) is an empty function in this guest and `useLights` (slot 37, fixed function) is not
  recorded.

### The boundary with the guest

With that rule there is no state to write back and nothing to invalidate: each step that changes
guest state is the guest's function, and each part the native path takes over leaves the same
guest words the guest's part would (the constant mask cleared, the RTSS parameters written, the
buffers uploaded, the counters added). What the native path changes in the backend is what the
guest's object would have changed, in the same order. Comparison mode checks it (section 3),
including the guest words each part must leave.

### The walk the guest keeps (facts the native path relies on)

From the disassembly, against OgreSceneManager.cpp and OgreRenderQueueSortingGrouping.cpp 1.7.0
(`guest_abi/render_queue.h`):
- `0x821DD098` (slot 26) is `renderBasicQueueGroupObjects`: it sorts each priority group's six
  collections (`QueuedRenderableCollection::sort` 0x821DCFC8) and draws solids basic, transparents
  unsorted and transparents through `renderObjects` (slot 27, `0x821DD1C0`), which sets the
  visitor's fields (the `transparentShadowCastersMode` field is gone in Runic's build) and calls
  `acceptVisitor` 0x821C08B8.
- Runic's pass-grouped walk `0x821CC700` re-sorts each group's renderables with MSVC's `std::sort`
  by a float at Renderable +0x1C and visits the pass again when that float changes, starting from
  -1.0; a group's head run at -1.0 is drawn with the visitor's `mUsedPass`. The native path does not
  touch any of this: whatever pass the guest's walk set, the native object is drawn with it.
- The visitor's slot 1 (`0x821CCB88`) validates the renderable and calls `renderSingleObject`;
  slot 2 (`0x821D27F8`) validates the pass and calls `_setPass`; slot 3 (`0x821C0A50`) does both.
- The render queue listeners (`fireRenderQueueStarted`/`Ended`) are fired by `0x821BC5A0`
  (`renderVisibleObjectsDefaultSequence`), outside the walk.

### What the native path reads from the guest (all through `guest_abi`, big-endian, no casts)

- **The pass** (the one the guest's `_setPass` returned, passed to `renderSingleObject`): its
  programs and their GpuProgramParameters (the constant buffers and the automatic constant list,
  as `ogre_layout.h` already reads them for the capture), its iteration and light settings.
- **The renderable:** its RenderOperation (VertexData, IndexData, operation type; Runic's
  `SubEntity::getRenderOperation` 0x821A3DE8 ignores the entity's LOD index), its world matrices,
  its custom parameters and its light list. These come from virtuals (`getRenderOperation`,
  `getWorldTransforms`, `getCustomParameter`, `getLights`); the native path reads the data behind
  them only for the classes it knows by vtable, and only where that data is up to date: a node's
  cached transform and a movable object's light list are computed lazily, so the native path calls
  the guest's getter where the cache can be out of date (that only fills a memo the guest would
  fill the same way).
- **The automatic parameter source:** the camera, the viewport and the scene-wide values the
  guest's AutoParamDataSource holds from `_renderScene` and the current pass from `_setPass`; the
  per-object values the native path computes itself.

### Bit identity

Comparison mode asks for every constant bit for bit. The guest computes its derived matrices
(world-view-projection and the like) with recompiled PowerPC arithmetic, fused multiply-adds
included, and the order of operations decides the bits (docs/game-thread-parallelism.md). What
must be matched is what ReXGlue emitted for those instructions, not the PowerPC semantics: for
example `fmadds` becomes a double fused multiply-add rounded to float (two roundings), which
`fmaf` would not reproduce, and VMX code flushes denormals. The native code copies, for each
automatic constant it supports, the operations the recompiled `_updateAutoParams` and the
AutoParamDataSource getters perform, in their order, in a translation unit built without floating
point contraction, setting the flush-to-zero mode for each block as the emitted code does, with no
tolerance. It must give the same bits on x86-64 (with and without hardware FMA), arm64 and MSVC,
so comparison mode runs on the desktop and on the POCO. A constant type that cannot be made
identical stays unsupported, and objects whose pass uses it fall back.

### What equivalent means

Every draw must get the same **effective state**: the state the backend holds when it executes
the draw (target, viewport, every render state, every texture and sampler, the declaration and
buffers, every constant register, the draw parameters). The command streams may differ (the
guest uploads only dirty ranges; the session's memo drops repeats), the effective state may not.

### Outside the cut, for later

The GUI is not part of this plan. It is drawn by the game's render queue listener
(`fireRenderQueueStarted` 0x821BC468 -> `0x821FBC60`, CEGUI's window tree and renderer), outside
the walk, and it is what grows most between wave 1 and wave 10 of `fight5` (1.8 -> 6.0 ms). The
code reading so far: every item on the floor has one CEGUI label window
(`ceguiWidget/ItemTextButton`, created the first time it is shown, kept in the item at +0x198);
hidden labels are removed from the window tree, so they are neither walked nor drawn; shown labels
(items within a radius of the player, when the item names setting or key ask for them) are
repositioned every frame, and a shown label outside the screen is clamped to its edge rather than
dropped. How many labels are shown per wave is not measured. Work on the loot labels and the GUI
comes after this plan, as does native culling (section 2).

## 2. Stages

The ceiling of this design, from stage 0's census and the profile (POCO X7, 2026-10-23): the
per-object work is 70.6 % of the walk in `fight.txt` (autoparams 0.86, constants upload 1.13, the
draw guard and its capture 1.40, RTSS per object 0.61, vertex buffers 0.53, the rest of
`renderSingleObject` 1.25 of 8.19 ms) and 73.3 % in wave 10 of `fight5`. Split by draw type with the
census's draw shares and walk (commit 79baf91: 270 queue draws and 8.1 ms of walk a frame in
`fight.txt`, 531 and 12.0 ms in wave 10; the profile's 8.2 / 13.2 ms were other runs), less 3-5 us a
native draw (a guess until stage 1 measures it):

| Draw type | `fight.txt`: per-object ms / gain | wave 10: per-object ms / gain | What it needs reproduced |
| --- | --- | --- | --- |
| Static opaque, main scene | 1.75 / 1.3-1.5 | 3.74 / 2.6-3.1 | the per-object automatic constants, RTSS per-object states, the draw |
| Projector passes | 1.65 / 1.3-1.4 | 1.48 / 1.0-1.2 | the same, with the projector's camera and target; texture projector constants |
| Transparents | 1.46 / 1.1-1.25 | 3.09 / 2.2-2.5 | billboard and particle renderers (dynamic buffers the guest fills, their classes); the sort and the per-entry pass visits stay the guest's |
| Skinned | 0.89 / 0.7-0.8 | 0.50 / 0.35-0.4 | the bone palette `cacheBoneMatrices` left (several world matrices), hardware skinning's RTSS state |
| **All** | **5.74 / 4.4-4.9** | **8.82 / 6.2-7.2** | |

The shares are prorated by draws inside each collection, so skinned draws (which likely cost more
each) are undercounted and the others overcounted; the totals hold. `fight.txt`'s guest frame is
about 22 ms: even all four types together leave about 17 ms, short of 16.6 ms (60 fps), so a
second front is needed besides this plan: native culling (the octree walk, 2.9 ms in `fight.txt`,
4.5 ms in wave 10) or the GUI (1.6 / 6.0 ms).

Each stage is behind the runtime switch, falls back per object to the guest's
`renderSingleObject`, and is integrated only after comparison mode shows no difference on the
replays and on the device. The stages take the per-object parts one at a time (the boundary rule,
section 1), first for the static opaque objects of the main scene, then widen to the other draw
types. The parts' share of the walk in `fight.txt` / wave 10 (table 1): constants (automatic
parameters and upload) 1.99 / 3.05 ms; RTSS per object 0.61 / 1.03; the draw and its capture 1.93 /
3.19 (the guard, `_render` and vertex buffers); the rest of `renderSingleObject` 1.25 / 2.37.

| Stage | What | Gain, `fight.txt` / wave 10 (upper bound) | Risk |
| --- | --- | --- | --- |
| 0 | `guest_abi` entries and the queue census (done, PR #47); a census of the automatic constants, RTSS sub-render states and texture unit kinds the stage 1 objects use, to fix its lists; the reading of `renderSingleObject` and the functions it calls, part by part; the comparison harness with an empty native path | 0 | Low |
| 1 | The constants, natively, for the static opaque objects of the main scene: objects whose pass and renderable qualify, each criterion read from guest data: the render target is the window; solids basic; vertex and fragment programs, no geometry program; automatic constants all in a supported list; one pass iteration, no iterate-per-light, no run-only-for-one-light-type, automatic lights and no manual light list; a renderable of a known class by vtable (SubEntity, StaticGeometry's GeometryBucket), a SubEntity's entity with no bone matrices and no vertex animation; the illumination stage none and render state changes not suppressed | about 0.5 / 0.8-0.95 ms (the constants' share of those objects' walk, 30.4 % / 42.5 % of the draws, less 1-1.5 us a native object) | Low-medium: one part, a pure computation, every other step the guest's |
| 2 | The constants for the other draw types (projectors, transparents, skinned: several world matrices) | +1.1-1.2 / +1.1-1.3 ms | Low-medium |
| 3 | RTSS per object natively, for the supported sub-render states | +0.5 / +0.7-0.8 ms | Medium |
| 4 | The draw and its capture natively | +1.4-1.65 / +1.9-2.4 ms | Medium-high (buffer uploads and snapshots, clip planes, iteration loop) |
| 5 | The rest of `renderSingleObject` natively | +1.0-1.05 / +1.6-1.8 ms | Medium |

The gains assume the census's draw shares apply to every part alike and split the guessed 3-5 us of
native cost per draw among the parts (constants 1-1.5, RTSS 0.3-0.5, the draw 1-2, the rest 0.7-1);
together they reach the ceiling of the table above (4.4-4.9 / 6.2-7.2 ms). Stage 1 is small on
purpose: it builds the machinery (the per-object hook, the comparison mode, the native automatic
constants with bit identity) on the part with the least guest state. None of the stages touches the
game's behaviour: the dead, the loot and every object keep being updated and drawn as now.

## 3. Verification and the A/B switch

**Comparison mode** (a `--diag_` option, off by default, in the diagnostics build):
- For each object the native path would handle, it computes and records what it would emit,
  without emitting it, then calls the guest's `renderSingleObject`, whose commands the capture
  records and sends as today. Both start from the same pass state (the guest's), so the effective
  state of the object's draw is compared directly: target, viewport, every render state, every
  texture binding with its sampler state, the declaration and bindings, the index range, every
  constant register bit for bit, and the draw parameters.
- The first difference of a frame is logged with the pass, the renderable's class and address,
  and the field that differs. A per-run counter reports how many draws matched, differed or fell
  back, by reason.
- Guest words: after the guest's call, the words the native parts must leave (SceneManager
  +0x43B0 and +0x43B4, the AutoParamDataSource's light list, RS +0x74, +0x298, +0x2A0..+0x2AC and
  +0x2F4, the counters) are compared with what the native path computed it would leave; the
  native path predicts the guest calls it would make (`preRender`/`postRender`, the listener)
  without making them in this mode.
- A texture first used in a frame is loaded by `touch` inside the guest's pass visit, before the
  object, so it is loaded for both paths.
- Comparison mode runs both, so its frame times say nothing about the gain. It runs on the desktop
  and on the POCO (bit identity).
- A stage becomes the default only after fights, the town and a few levels show no difference,
  over a whole session.

**Runtime switch:** `--native_render_queue` (off / on, later per stage). It lives in the same
binary as the guest path, so an A/B turns it on and off without rebuilding, and the build-layout
shift between binaries does not apply (docs/perf/2026-10-22-overnight.md, "Method"). A/Bs follow
the usual rules (A B B A, thermal state recorded, the fight named).

**Replays:**
- An F9 capture records the commands, not the guest's queue, so a replay cannot run the native
  path itself, and two live runs never draw the same frames: the equivalence proof is the
  comparison mode, inside one frame. In that mode an F9 capture also stores the native stream next
  to the guest's, and the replay tool compares the two offline.
- Captures taken with the native path on replay byte for byte on GL3+ and GLES as any capture
  does (CI's GLES job, Mesa); screenshots on the POCO at the same scenes are an image check.

## 4. Runic differences to reproduce

Known (in `guest_abi`, with addresses), and to keep exactly:
- One depth surface for every target: `_setRenderTarget` 0x821A7620 binds only a render
  texture's colour (`ogre_layout.h`, `render_system`).
- The inverted depth: `_setDepthBufferFunction` 0x821C3010 swaps LESS and GREATER, and the
  viewport's depth range is 1..0 (`ogre_enums.h`, `RunicEffectiveDepthFunction`,
  `kRunicViewportMinZ`).
- The instance count in RenderOperation +0x14, which `_render` 0x821CEA60 multiplies the primitive
  count by (`ogre_layout.h`, `kRunicInstanceCount`).
- The vertex buffers' instance data fields (+0x28, +0x2C) and the vertex declaration's list layout
  (`ogre_layout.h`).
- `_updateAutoParams` 0x82201EB0: four automatic constant types fall to the default label,
  although upstream handles them (`ogre_enums.h`). The native implementation must do the same:
  write nothing for them.
- The texture's Runic slot 72 (0x8257B6E0, bit 0x200) and the D3D9RenderSystem's extra slot 89,
  which shifts the later slots.

To find in stage 0, by reading the code, not by assuming OGRE 1.7:
- `renderSingleObject` 0x821C5FA0: the light iteration, the per-object listener call, the custom
  parameters, the world matrix count, the per-object render system calls and their conditions;
- the RTSS listener's per-object updates (0x821C4F50 and the sub-render states it calls), which
  change constants per object.
(`_setPass` and the walk stay the guest's and need no reproduction.)

## 5. Risks

- **A difference the comparison does not see:** for example a state that only matters on some
  driver. The comparison checks every field of every command, and replays and device screenshots
  cover the images.
- **A part that leaves guest state differently:** each native part must leave the guest words
  its guest part leaves (section 1). Comparison mode compares those words after every object, so
  a difference shows in the same frame.
- **Bit identity:** a derived constant computed in another order differs in its last bits. The
  native code copies the guest's order; a type that cannot be made identical stays unsupported
  (section 1).
- **Threading:** the native per-object work runs on the guest's thread, inside the guest's walk;
  moving it to another thread (the guest thread is the one that bounds the frame) is not part of
  this plan.
- **Virtuals of classes not known:** renderables of classes the native path does not know fall
  back, counted by class in the comparison mode, so the coverage is measured, not assumed.
- **Mid-frame changes:** objects or passes changed by a listener during the walk. Listeners on the
  render queue keep running in place (the walk is the guest's), and an object with a render object
  listener other than RTSS's falls back. The game's own listener draws the GUI from
  `renderQueueStarted` (above); its draws reach our capture as today.
- **Maintenance:** a second implementation of OGRE 1.7's per-object draw path, kept equal by the
  comparison mode, which stays in the diagnostics build for as long as the native path exists.
- **The previous attempt:** the legacy port tried a detached OGRE. This plan does not reuse its
  architecture (CLAUDE.md). Facts from it are checked again in the code before they are used.

## What stage 0 still needs

- Done (PR #47): `guest_abi/render_queue.h` and the queue census (`--diag_queue_census`).
- A census of what the stage 1 objects use: their automatic constant types, their RTSS sub-render
  states (by vtable) and their texture unit kinds, to fix stage 1's supported lists.
- The reading of `renderSingleObject` 0x821C5FA0, `fireRenderSingleObject` 0x821C4EE0,
  `useLightsGpuProgram` 0x821C12C0 and `updateGpuProgramParameters` 0x821C26D0 against
  OgreSceneManager.cpp 1.7.0, line by line, with the per-object state each leaves; and of the RTSS
  sub-render states' `updateGpuProgramsParams` for "last value" caches (harmless while the native
  path never writes the guest's GpuProgramParameters buffers).
- The comparison harness with an empty native path (every object falls back), measured with the
  option off to show it costs nothing.
