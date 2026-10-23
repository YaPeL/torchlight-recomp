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

### The cut point: `renderSingleObject`

`SceneManager::renderSingleObject(renderable, pass, scissoring, autoLights, manualLightList)`
(slot 21, `0x821C5FA0`) is called by the visitor for each object, after the guest's `_setPass` for
its pass. The native path takes over that call for each object it handles and leaves every other
object to the guest's function, so the fallback is **per object**: the pass state is the guest's
either way, so a native object and a guest object of the same pass see the same pass state. (Stage
0's census found no pass group mixing objects the native path would handle with objects it would
not, so in practice whole groups go one way or the other.)

For a native object the native path does what `renderSingleObject` and the functions it calls do
for that object, and emits the same per-object commands through the same recording functions:
- the RTSS per-object listener (`fireRenderSingleObject`, slot 13, `0x821C4EE0`, and
  `notifyRenderSingleObject` 0x821C4F50), for the sub-render states it supports, computing their
  per-object values without writing the guest's GpuProgramParameters buffers;
- the light list and the light-dependent constants (`useLightsGpuProgram`, slot 39, `0x821C12C0`);
- the automatic constants of the per-object (and, when the lights changed, light) ranges, as
  `updateGpuProgramParameters` (slot 41, `0x821C26D0`) and `_updateAutoParams` 0x82201EB0 compute
  them;
- the per-object render system calls the guest makes unconditionally on this path (pass iteration
  count, derived depth bias, polygon mode with the camera's override);
- the vertex declaration, buffer bindings and draw of the `_render` guard 0x821C4058, and the
  renderable's `preRender`/`postRender`;
- the counters `_render` adds to (RS +0x84/+0x88/+0x8C).

### The boundary with the guest

The guest's per-object state that a native object leaves stale is small, read from
`renderSingleObject`:
- the light hash `mLastLightHashGpuProgram` (SceneManager +0x43B0), which `useLightsGpuProgram`
  compares to skip the light constants: after a native object it is set to 0, so the next guest
  object uploads its light constants (a native object's lights may differ from those the guest
  last uploaded);
- `mGpuParamsDirty` (+0x43B4): the next guest object marks its own per-object ranges dirty at its
  start (`sth` @0x821C6168), and its pass visit marks everything dirty, so nothing to write;
- the AutoParamDataSource's per-object fields (current renderable, world matrices and their
  derived caches): the next guest object sets them before it reads them;
- the render system's per-object members (pass iteration count +0x298, derived depth bias
  +0x2A0.., cull mode +0x74): the next guest object sends the iteration count and the depth bias
  itself; the cull mode only changes for negatively scaled objects, which stage 1 leaves to the
  guest; multi-iteration passes, which read +0x298 inside the guard, are left to the guest too.

What a native object changes in the backend is only per-object state (per-object and light
constant ranges, the declaration and buffers, the draw); the next guest object re-sends all of it
(the guard binds the declaration and buffers every draw, the object's ranges are dirty, the light
ranges are re-sent because of the hash), and a pass visit re-sends everything. Guest code after the
walk (the GUI's renderer, the listeners) sets its own state and does not read constants; the
projector passes and the next frame start with pass visits. Native constants are stored in the
same constant mirror as the hooks' (`capture/constant_mirror.h`), and the native path makes the
same resource announcements (textures, declarations, programs, buffer snapshots).

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
`renderSingleObject` 1.25 of 8.19 ms) and 73.3 % in wave 10 of `fight5`. Split by draw type with
the census's draw shares (270 queue draws and 8.1 ms of walk a frame in `fight.txt`, 531 and
12.0 ms in wave 10), less 3-5 us a native draw (a guess until stage 1 measures it):

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
replays and on the device. Ordered by gain over risk:

| Stage | What | Gain, `fight.txt` / wave 10 | Risk |
| --- | --- | --- | --- |
| 0 | `guest_abi` entries and the queue census (done, PR #47); a census of the automatic constants, RTSS sub-render states and texture unit kinds the qualifying objects use, to fix stage 1's lists; the comparison harness with an empty native path | 0 | Low |
| 1 | Static opaque objects of the main scene: objects whose pass and renderable qualify, each criterion read from guest data: the render target is the window; solids basic; vertex and fragment programs, no geometry program, programs that do not ask for transform states; one pass iteration, no iterate-per-light, no run-only-for-one-light-type, no light scissoring or clip planes, no depth bias; automatic constants all in a supported list and RTSS sub-render states all in a supported list keyed by their vtables; a renderable of a known class by vtable (SubEntity, StaticGeometry's GeometryBucket), a SubEntity's entity with no bone matrices and no vertex animation; no render object listener other than RTSS's; the illumination stage none and render state changes not suppressed; no negative scale, no identity view or projection | 1.3-1.5 / 2.6-3.1 ms | Medium |
| 2 | Projector passes: the same objects in render textures, with the texture projector constants | +1.3-1.4 / +1.0-1.2 ms | Medium (stage 1's machinery, another camera) |
| 3 | Transparents: billboards, particle renderers and the rest of the sorted walks' objects | +1.1-1.25 / +2.2-2.5 ms | Medium-high |
| 4 | Skinned objects: several world matrices, the hardware skinning state | +0.7-0.8 / +0.35-0.4 ms | Medium |

Stage 3 comes before stage 4 although its risk is higher: it is worth much more in the long fight,
where the transparents grow and the skinned draws do not. None of the stages touches the game's
behaviour: the dead, the loot and every object keep being updated and drawn as now.

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
- Side effects: the native path records the ones it performs per object (`preRender`/`postRender`,
  listener calls, the counters' increments) and so does the guest's call; they must be equal.
- A texture first used in a frame is loaded by `touch` inside the guest's pass visit, before the
  object, so it is loaded for both paths.
- Comparison mode runs both, so its frame times say nothing about the gain. It runs on the desktop
  and on the POCO (bit identity).
- **Boundary check** ("on" mode, diagnostics build): after each native object, the session marks
  the state slots the object wrote (its constant ranges, the declaration and buffers); the next
  guest draw must have re-sent every marked slot (a guest call clears a mark at hook entry, before
  any memo or mirror filter); a marked slot left at a guest draw is logged. Per the boundary
  section, the guest re-sends all of them, so any report is a hole.
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
- **Hidden reads of the guest's per-object state:** guest code that reads what
  `renderSingleObject` leaves (section 1, the boundary). The list is short and comes from reading
  that function; the boundary check in the "on" mode reports any slot a guest draw inherits from a
  native object.
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
