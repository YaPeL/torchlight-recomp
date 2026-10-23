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

The idea: read the guest's render queue after culling and emit our commands for it directly,
instead of letting the guest's OGRE turn each object into D3D9 calls that our hooks then turn
back into commands. The commands are the same ones the capture produces today
(`commands/types.h`), so the backend, the frontend and the replay do not change, and the two
paths can be compared command by command.

The 2026-10-13 study rated this "F2, native pass renderer" as high risk and XL
(docs/game-thread-parallelism.md). The risk is still there; this plan contains it by going in
stages, each with a fallback to the current path for whatever it does not handle and a
verification mode.

## 1. Where to cut

### What the guest keeps

Everything up to the end of culling stays the guest's, unchanged:
- the game's logic and the scene graph (`_updateSceneGraph`);
- the animation, including `Entity::cacheBoneMatrices` 0x821B93B0, whether the game calls it or
  `Entity::_updateRenderQueue` 0x821E0D18 does;
- the controllers (`ControllerManager::updateAllControllers` 0x821C8C00) and the particle
  systems;
- culling (`_findVisibleObjects`: OctreeSceneManager vtable 0x82005B9C slot 122, octree walk
  0x821C7158) and each object's `_updateRenderQueue`, which fills the render queue;
- the render target and viewport setup of `_renderScene` 0x821B07F0 (and its viewport clear),
  which our hooks already capture.

The cut is after the queue is filled and before it is drawn. Anything the native path does not
handle goes through the guest's code exactly as today.

### The cut point

From the disassembly, against OgreSceneManager.cpp and OgreRenderQueueSortingGrouping.cpp 1.7.0:
- `0x821DD098` (SceneManager vtable slot 26) is `renderBasicQueueGroupObjects`. For each
  priority group it first sorts the group's six collections (`RenderPriorityGroup::sort`, inlined:
  six calls of `QueuedRenderableCollection::sort` 0x821DCFC8, which radix-sorts above 2000 entries
  and stable-sorts below, on the collections at +0x08, +0x58, +0x30, +0x80, +0xA8, +0xD0, 0x28
  apart), then draws three of them through `renderObjects` (slot 27, `0x821DD1C0`): solids basic
  (+0x08), transparents unsorted (+0xA8) and transparents (+0xD0, always descending), as upstream.
- `renderObjects` 0x821DD1C0 sets the active visitor's fields (at SceneManager +0x433C: `autoLights`
  +0x0C, `manualLightList` +0x10, `scissoring` +0x14; upstream's `transparentShadowCastersMode` sits
  at +0x0C with `autoLights` after it: the field is gone in Runic's build, a Runic difference) and
  tail-calls `QueuedRenderableCollection::acceptVisitor` 0x821C08B8. That function is upstream's,
  with the descending and ascending walks inlined over the sorted `std::vector<RenderablePass>` at
  +0x18/+0x1C (8 bytes an entry) and the pass-grouped walk in `0x821CC700`. The sort above only acts
  on collections whose organisation mode has the descending bit (0x2).
- **Runic's pass-grouped walk** (`0x821CC700`; in wave 1 of `fight5` the samples under it come to
  5.2 ms a frame of the 9.1 ms under `acceptVisitor`, counted inclusively, not by table 1's rule) is
  not upstream's. For each pass of the map (node key +0x0C, the RenderableList* at +0x10) it copies
  the renderables into a static vector (0x83582B5C), sorts them with MSVC's `std::sort`
  (`0x823497E0`, an introsort, not stable) by a float at Renderable +0x1C, ascending (comparator
  `0x82190520`), and walks them: `visit(Pass*)` (so `_setPass` again) every time that float differs
  from the previous one's, starting from -1.0 (the constant at 0x820AA334), then
  `visit(Renderable*)`. A false from the pass visit ends the group. Renderables whose float is
  exactly -1.0 at the head of a group are drawn with the visitor's `mUsedPass` (+0x04) as the
  previous group left it. What Runic stores at +0x1C is to be read in stage 0. The native path calls
  the guest's own `std::sort` on the same copy, so the order is the guest's, and keeps the -1.0
  start and the pass re-visit.
- The visitor (`SceneMgrQueuedRenderableVisitor`; the overloads sit in reverse order,
  `ogre_layout.h`): slot 1 `0x821CCB88` = visit(Renderable*): `validateRenderableForRendering`
  (SceneManager vt+0x10), then `renderSingleObject` (vt+0x54); slot 2 `0x821D27F8` =
  visit(const Pass*): `validatePassForRendering` (vt+0x0C), then `_setPass` (vt+0x398); slot 3
  `0x821C0A50` = visit(RenderablePass*): `validateRenderableForRendering`, `_setPass`,
  `renderSingleObject` (as upstream, which never validates the pass there; Runic drops upstream's
  `transparentShadowCastersMode` test, with the field itself, gone from the visitor). In this guest
  `validateRenderableForRendering` (slot 4, `0x828AD7C0`) returns true and
  `validatePassForRendering` (slot 3, `0x821D2880`) only checks late material resolving
  (vt+0x2FC); the native path reproduces both from those bodies.
- The cut is therefore **`renderObjects`, one collection at a time**. The guest's collection sort
  stays; inside the grouped walk the native path calls the guest's `std::sort` (above). The native
  path walks the collection itself and decides **per pass group** (grouped walk) or per entry
  (sorted walks) whether it handles the work or hands that group to the guest's visitor. Stage 0's
  census (POCO X7, 2026-10-23, `--diag_queue_census`) chose that granularity: no pass group ever
  mixed objects the native path would handle with objects it would not (a pass belongs to one
  material, and static and skinned objects never share one), while the collections that mix are
  exactly the large ones of the main scene, so a fallback by whole collection keeps far less native
  (section 2). Every switch between native and guest therefore falls at a pass visit, and no group
  in those runs started its walk at -1.0 (such a group falls back; see the boundary).
- The listeners on the render queue (`fireRenderQueueStarted`/`Ended`) are fired by `0x821BC5A0`
  (`renderVisibleObjectsDefaultSequence`) around `renderQueueGroupObjects`, outside the cut; they
  keep running as today.

### What the native path reads from the guest (all through `guest_abi`, big-endian, no casts)

- **The render queue:** RenderQueue -> RenderQueueGroup -> RenderPriorityGroup -> its six
  QueuedRenderableCollection (pass-grouped `std::map<Pass*, RenderableList*, PassGroupLess>`,
  ordered by `Pass::getHash`; sorted `std::vector<RenderablePass>`). The MSVC `std::map` and
  `std::vector` layouts are already confirmed in `guest_abi/ogre_layout.h`.
- **The pass:**
  - the scene blend, depth check/write/function, cull mode, alpha rejection, colour write and
    polygon mode;
  - the TextureUnitState list: texture, addressing, filtering, the texture matrix where used;
  - the vertex and fragment program usages and their GpuProgramParameters, i.e. the constant
    buffers and the automatic constant list (`ogre_layout.h` already reads these for the capture:
    `_updateAutoParams` and `bindGpuProgramParameters`).
- **The renderable:**
  - its RenderOperation: the VertexData (declaration, buffer bindings), the IndexData and the
    operation type. The capture reads these today in `ReadVertexBufferBinding` and the `_render`
    guard (Runic's `SubEntity::getRenderOperation` 0x821A3DE8 ignores the entity's LOD index);
  - its world matrices;
  - its custom parameters;
  - its light list.

  These four come from virtuals of the guest's objects (`getRenderOperation`,
  `getWorldTransforms`, `getCustomParameter`, `getLights`). The native path reads the data behind
  them directly only for the classes it knows: SubEntity, StaticGeometry's GeometryBucket, later
  BillboardSet and ParticleUniverse's renderers. For any other class it falls back.
- **The automatic parameter source:** the native path builds it itself, from the camera and its
  matrices, the current lights, the fog, the ambient colour and the time, as `_setPass` and
  `renderSingleObject` would set the guest's `AutoParamDataSource` up. It reads that object only
  for what `_renderScene` sets before the walk (the camera, the viewport, the scene-wide values);
  its per-pass and per-object fields hold the previous pass's values when the native path runs
  (the native path sets only the current pass, at the end of a native stretch, below).

### The boundary with the guest's state

The guest's walk leaves state behind that later code reads, and its own caches assume that every
pass went through it:
- `_setPass` and `renderSingleObject` keep the last light hash, the GPU parameters' dirty flags and
  the bound programs; the render system keeps the bound-program flags, the first disabled texture
  unit and the cull mode above our hooks, and the D3D9 state caches below them; our session drops
  state that did not change since the last call.
- Side effects to keep: `_setTexture` calls Runic's `touch` (`ogre_layout.h`), which loads the
  texture and notifies its manager; `renderSingleObject` calls the renderable's `preRender` and
  `postRender` and any render object listener (RTSS's and others); `_render` counts batches, faces
  and vertices (RS +0x84/+0x88/+0x8C), to which every native draw adds too; the visitor's
  `mUsedPass` (+0x04) is read by the next grouped walk (above). To read in stage 0 as well, from
  upstream's `renderSingleObject` and `_setPass`: the culling flip for negatively scaled objects,
  the re-issue of view-dependent texture coordinate generation, and the camera's polygon mode
  override.

The native path sends every state through the session's recording functions, the same ones the
hooks use, and stores its constants in the same constant mirror (`capture/constant_mirror.h`), so
the session's memo, the mirror and the backend always agree: nothing is restored, and the backend
simply holds the last native pass's state when the native stretch ends. It also makes the resource
announcements the hooks make today (textures, vertex declarations, programs, buffer snapshots).
What could go wrong is guest code skipping a call because one of the guest's own caches still
describes the state from before the stretch.

**What the next guest pass visit and its first object send unconditionally** (read from
`_setPass` 0x821C5730 and `renderSingleObject` 0x821C5FA0): the programs, through Runic's
`SceneManager::bindGpuProgram` (slot 40, `0x821C14B8`), which also resets the light hash (+0x43B0
= 0), the AutoParamDataSource's light list (vt+0x10 with null, a Runic difference) and
`mGpuParamsDirty` (+0x43B4 = `GPV_ALL`, a u16), so the first object uploads every constant; the
scene blend, depth function, check, write and bias, alpha rejection, colour write, cull mode,
shading and polygon mode; each texture unit up to the pass's count; per object, the pass iteration
count and the derived depth bias. **What they send only conditionally**, and how each is kept
right:
- `_disableTextureUnitsFrom` (slot 34) disables only the units between the new count and the
  render system's own cached value (`mDisabledTexUnitsFrom`, RS +0x294): written back to 16 at
  the end of a stretch, so the next call disables every unit past its count;
- the cull mode per object (flip for negative scale) compares with the render system's cull mode
  member through `_getCullingMode`: stage 1 excludes negative scale, and the member is written
  back to the cull mode the native path left;
- the world matrices, only when the vertex program asks for transform states: stage 1 requires
  programs that do not;
- point sprites (only with the capability) and border colour (only for border addressing): the
  native path sends them under the same conditions, so they are right whenever they matter;
- lighting, surface and fog: empty functions in this guest's render system (slots 14, 35, 69);
- what the walk never sends (stencil, scissor, clip planes, view and projection, ambient light):
  the native path never changes it.

At the end of each native stretch the native path therefore writes: the light hash = 0, the
AutoParamDataSource's light list = null (the virtual call `bindGpuProgram` makes), `mGpuParamsDirty`
= `GPV_ALL` (16 bits), the vertex and fragment bound-program flags (RS +0x2E0..; not the geometry
one, which would emit a spurious unbind), `mDisabledTexUnitsFrom` = 16, the render system's cull
mode member, and the visitor's `mUsedPass` and the AutoParamDataSource's current pass (vt+0x28) =
the last native pass. For the guest's next group most of these are redundant (its pass visit runs
`bindGpuProgram` anyway); they matter for code that draws without a pass visit of its own after the
walk (the GUI's renderer, which calls `_disableTextureUnitsFrom(1)`, and the other listeners).

**The -1.0 head run.** A guest group whose walk starts at -1.0 draws its head run with no pass
visit, so with the per-pass state `_setPass` keeps (`mPassCullingMode` +0x41A0, the pass number,
the texture projectors), which would still describe the last guest pass. Rule: a native stretch
never ends just before such a group; the native path either draws that group too or ends the
stretch one group earlier. The census found none.

### Bit identity

Comparison mode asks for every constant bit for bit. The guest computes its derived matrices
(world-view-projection and the like) with recompiled PowerPC arithmetic, fused multiply-adds
included, and the order of operations decides the bits (docs/game-thread-parallelism.md). What must
be matched is what ReXGlue emitted for those instructions, not the PowerPC semantics: for example
`fmadds` becomes a double fused multiply-add rounded to float (two roundings), which `fmaf` would
not reproduce, and VMX code flushes denormals. The native code copies, for each automatic constant
it supports, the operations the recompiled `_updateAutoParams` 0x82201EB0 and the
AutoParamDataSource getters perform, in their order, in a translation unit built without floating
point contraction, setting the flush-to-zero mode for each block as the emitted code does (it
toggles it per block), with no tolerance. The repository sets no floating point contraction flag
today: the generated code is one operation per statement with explicit fused multiply-adds, which
clang keeps as written, but GCC and MSVC may contract across statements, so pinning the generated
code's flags is a separate build change with its own validation. It must give the same bits on
x86-64 (with and without hardware FMA), arm64 and MSVC, so comparison mode runs on the desktop and
on the POCO. A constant type that cannot be made identical stays unsupported, and passes that use it
fall back.

### What equivalent means

The capture's command stream is shaped by caches (the guest's own and our session's memo) that
depend on earlier collections and frames, so the native path is not required to emit the same
commands. It is required to give every draw the same **effective state**: the state the backend
holds when it executes the draw (target, viewport, every render state, every texture and sampler,
the declaration and buffers, every constant register, the draw parameters). The native path sends
state through the same recording functions as the hooks, so our session's memo drops what did not
change in both paths alike; comparison mode compares the effective state per draw.

### What it emits

Commands of the same types the capture emits, through the same recording functions, giving each
draw the same effective state ("What equivalent means", above):
- `SetRenderState` and the other state commands;
- `SetTexture` and the sampler states;
- the vertex declaration and buffer bindings;
- `SetConstants` with the same ranges;
- `Draw`.

The resources (buffers, textures, programs) are the ones the capture already announces and
mirrors by guest identity. The native path refers to them through the same registry and does not
create any of its own.

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
comes after this plan.

## 2. Stages

Each stage is behind a runtime option (below), falls back to the guest for everything it does
not handle, and is integrated only after its verification passes on the replays and on the
device. The gains are estimates from the profiles above, to be measured per stage. Stage 0
measured the share stage 1 could cover (below); the split between stages 2 and 3 is still a guess,
and the sum of stages 1 to 3 is bounded by the main scene's walk (draw path plus capture: 6.0 ms
in `fight.txt`, 11.0 ms in wave 10 of `fight5`). The ranges assume the covered draws lose all of
their guest and capture cost except the native walk's own, which is guessed at 3-5 us per draw
(it includes the automatic parameters, on which the guest spends about 2.7 us a draw; a guess until
stage 1 measures it). The upper ends need full coverage and about 3 us a draw.

| Stage | What | Covers (to measure) | Estimated gain, `fight.txt` / `fight5` wave 10 | Risk |
| --- | --- | --- | --- | --- |
| 0 | `guest_abi` entries for the queue, the collection, the visitor, the pass and the renderables, with evidence; the census (`--diag_queue_census`, diagnostics build); the boundary list (the invalidation writes, the write-backs, the side effects); the comparison harness (section 3) with an empty native path (every group falls back) | measurement | 0 (the diagnostics are off by default and not in release builds) | Low; the census part done (PR #47) |
| 1 | Pass groups of the main scene's solids-basic collections (the render target is the window, not a render texture), falling back by pass group, where the pass and every object qualify, each criterion read from guest data: vertex and fragment programs, no geometry program; one pass iteration and no iterate-per-light, light scissoring and light clip planes off; no depth bias (constant, slope, per iteration); texture units of plain content (no shadow or compositor content, no animated or effect units, no vertex texture binding); automatic constants all in a supported list and an RTSS SGPass whose sub-render states are all in a supported list keyed by their vtables (both lists fixed from a census of what the qualifying groups use); renderables of the known classes by vtable (SubEntity, StaticGeometry's GeometryBucket), a SubEntity's entity with no bone matrices and no vertex (morph or pose) animation; no render object listener other than RTSS's; no run-only-for-one-light-type; vertex programs that do not ask for transform states; the SceneManager's illumination stage none and render state changes not suppressed; camera-relative rendering off; no negative scale, no view-dependent texture coordinate generation, no camera polygon mode override, no identity view or projection; a group whose walk starts at -1.0 falls back; Runic's grouped walk reproduced (the guest's sort, the +0x1C re-visit) | census (commit 79baf91; its runs: 270 queue draws and 8.1 ms of walk a frame in `fight.txt`, 531 and 12.0 ms in wave 10 of `fight5`), an upper bound for the criteria it checks: 30.4 % of the draws and, prorated by draws, 29.6 % of the walk in `fight.txt`; 42.5 % and 37.4 % in wave 10 | about 2.0-2.2 ms / 3.4-3.8 ms (the draw-prorated share of the walk less 3-5 us a native draw; skinned draws likely cost more each, so the static groups' share may be lower) | Medium-high |
| 2 | Skinned entities: bone matrices as `cacheBoneMatrices` left them, the same palette upload | the characters, the dead included | +1-1.5 ms / +2-3 ms | Medium |
| 3 | Transparent passes in their sorted order, the RTSS sub-render states not supported in stage 1, billboards and particle renderers | effects, decals, UI in the world | +0.5-1 ms / +1-1.5 ms | Medium-high |
| 4 | The projector passes (shadow and light textures, their own render targets and cameras) | the 2 projector passes | +1.5 ms / +1.4 ms (their walk: 2.2 / 2.1 ms, less a native walk taken as 0.7 ms, more than the other rows assume) | Medium-high |
| 5 | Culling and the scene graph update natively (the octree walk, `_updateSceneGraph`), feeding the native walk directly | culling, graph | up to the culling, 2.9 ms / 4.5 ms, less the native culling's cost; the graph update not measured apart | High |

Stages 1 to 4 together aim at the 8.2 ms of the walk: around 5-6.2 ms of it back in `fight.txt`, and
about 7.8-9.7 ms of the 13.2 ms in wave 10 of `fight5`. Stage 5 is a separate decision after them
(it is the culling candidate C of the 2026-10-13 study). None of them touches the game's behaviour:
the dead, the loot and every object keep being updated and drawn as now. Only the cost of drawing
each one goes down.

## 3. Verification and the A/B switch

**Comparison mode** (a `--diag_` option, off by default, in the diagnostics build):
- The native walker drives, group by group (entry by entry in the sorted walks). For each group
  it would handle, it records what it would emit, without emitting it, and then hands that same
  group to the guest's visitor, whose commands the capture records and sends as today. At every
  place where the "on" mode would end a native stretch, it also computes the values it would
  write back and compares them with what the guest's walk leaves there.
- Comparison mode cannot see a missing invalidation: the guest walks every group there, so its
  caches are never stale. **Inherited-state check** (diagnostics build, "on" mode): at the end of
  each native stretch the session marks every state slot the stretch wrote (each render state,
  texture unit and sampler, matrix and constant register); a guest command for a slot clears its
  mark; a guest draw that executes while a slot it uses is still marked is logged with the slot,
  the draw and the last native pass, up to the end of the frame. Any guest cache missing from the
  list above shows up there as a draw using state the guest never sent, whatever cache it is.
- The two streams are compared per draw: target, viewport, every render state, every texture
  binding with its sampler state, the vertex declaration and bindings, the index range, every
  constant range bit for bit, and the draw parameters.
- The first difference of a frame is logged with the pass, the renderable's class and address,
  and the field that differs. A per-run counter reports how many draws matched, differed or fell
  back, by reason.
- Comparison mode runs both walks and the diff, so its frame times say nothing about the gain.
- A texture first used in a frame is loaded by `touch` inside the guest's walk, after the native
  path ran: the comparison counts those draws apart (a texture not yet loaded), not as
  differences.
- It runs on the desktop and on the POCO (bit identity across compilers and CPUs, section 1).
- Comparison mode records, so it does not check the side effects the "on" mode must perform
  (`touch`, `preRender`/`postRender`, the face, batch and vertex counts, listeners). The native
  path records each of them per kind and per renderable, and so does the guest path; in
  comparison mode the two records of the same group must be equal. In the "on" mode, the guest
  render system's own face and batch counters (+0x84/+0x88/+0x8C, to which every native draw adds)
  are compared statistically with the off mode's over the same scripted fight (two runs never draw
  the same frames).
- A stage becomes the default only after fights, the town and a few levels show no difference,
  over a whole session.

**Runtime switch:** `--native_render_queue` (off / on, later per stage). It lives in the same
binary as the guest path, so an A/B turns it on and off without rebuilding, and the build-layout
shift between binaries does not apply (docs/perf/2026-10-22-overnight.md, "Method"). A/Bs follow
the usual rules (A B B A, thermal state recorded, the fight named).

**Replays:**
- An F9 capture records the commands, not the guest's queue, so a replay cannot run the native
  path itself.
- Two live runs never draw the same frames (time, animation and AI differ), so the equivalence
  proof is the comparison mode, inside one frame. In that mode an F9 capture also stores the native
  stream next to the guest's, and the replay tool compares the two offline.
- What replays check besides: captures taken with the native path on replay byte for byte on GL3+
  and GLES as any capture does (CI's GLES job, Mesa).
- Plus screenshots on the POCO at the same scenes, as for the depth discard (an image check, not
  a byte check).

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
- `_setPass` 0x821C5730: what Runic changed in it, line by line against OgreSceneManager.cpp's
  `_setPass` (state order, which states it skips, the texture unit loop, shadow and fog handling);
- `renderSingleObject` 0x821C5FA0: the light iteration, the per-object listener call, the custom
  parameters, the world matrix count;
- the RTSS listener's per-object updates (0x821C4F50 and the sub-render states it calls), which
  change constants per object;
- the octree scene manager's queue organisation (which collections are filled, and the sort
  order of transparents).

## 5. Risks

- **A difference the comparison does not see:** for example a state that only matters on some
  driver. The comparison checks every field of every command, and replays and device screenshots
  cover the images.
- **Hidden reads of the guest's state:** code that reads the RenderSystem's, the
  AutoParamDataSource's or the pass's state after native work, expecting what the guest's walk
  left there: the next guest group inside the same frame as much as code after the walk. Stage 0
  lists the readers; at the end of each native stretch the native path invalidates the guest's
  caches with the writes Runic's own `bindGpuProgram` makes and writes back the current pass
  (section 1), and comparison mode checks the write-backs at every would-be end of a stretch.
- **Bit identity:** a derived constant computed in another order differs in its last bits. The
  native code copies the guest's order; a type that cannot be made identical stays unsupported
  (section 1).
- **Threading:** the native walk runs on the guest's thread, in place of the guest's walk, and
  reads the queue there; moving it to another thread (the guest thread is the one that bounds the
  frame) needs a snapshot of the queue and is not part of this plan.
- **Virtuals of classes not known:** renderables of classes the native path does not know fall
  back, counted by class in the comparison mode, so the coverage is measured, not assumed.
- **Mid-frame changes:** objects or passes changed by a listener during the walk. Listeners on the
  render queue (`fireRenderQueueStarted`/`Ended`, fired in 0x821BC5A0 outside the cut) keep running
  in place, and a group or entry whose objects have a listener that does something unknown falls
  back. The game's own listener draws the GUI from `renderQueueStarted` (above); its draws reach
  our capture as today.
- **Maintenance:** a second implementation of OGRE 1.7's draw path, kept equal by the comparison
  mode, which stays in the diagnostics build for as long as the native path exists.
- **The previous attempt:** the legacy port tried a detached OGRE. This plan does not reuse its
  architecture (CLAUDE.md). Facts from it are checked again in the code before they are used.

## What stage 0 still needs

- The disassembly of `renderObjects` 0x821DD1C0, `acceptVisitor` 0x821C08B8 and its grouped
  walk 0x821CC700, the visitor's three slots and `_setPass`/`renderSingleObject`, read against
  OgreSceneManager.cpp 1.7.0; `guest_abi` entries for the queue's structures, with evidence.
- The boundary list of section 1: the invalidation writes and the write-backs, and the readers
  of what the guest's walk leaves.
- The comparison harness, with the native path empty (everything falls back, every draw counted
  as "not handled"), measured with the option off to show it costs nothing.
- Done (PR #47): the census of how each collection's pass groups split into qualifying and not,
  on the POCO in `fight.txt` and `fight5.txt`. Still to do: the census of the automatic constants,
  RTSS sub-render states and texture unit kinds the qualifying groups use, to fix stage 1's lists.
- The RTSS sub-render states' `updateGpuProgramsParams`, read for "last value" caches: they are
  harmless only while the native path never writes the guest's GpuProgramParameters buffers, which
  it does not (it writes our constant mirror and the backend).


