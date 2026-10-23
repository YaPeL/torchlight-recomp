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

On the POCO X7 the game's thread is the limit. Walking OGRE's render queue costs 8.2 ms of its
22.1 ms frame in the 30-second fight (`fight.txt`, 25 s profiled from 2.6 s after the mark, about
300 draws a frame): about 27 us a draw, 17 of them in the guest's embedded OGRE (passes, automatic
parameters, constants, texture units, the per-object work) and 10 in our hooks and capture of
each D3D9 call (`ReadConstants`, the session's state checks, the resource registry, vertex buffer
reads). In the 5-minute fight (`fight5.txt`, 11 s profiled in the middle of wave 1 and at the end
of wave 10) the same walk is 9.3 ms of a 25.2 ms frame in wave 1 and 13.2 ms of 39.5 ms in wave
10. The dead and the loot are never removed in that fight, so the draws grow from about 300 to
about 689 a frame by wave 10: about 19 us a draw there. (The draws a frame are the frame
counter's, from the twelve `fight5` runs of 2026-10-22 on binary 03ab9ae, tag
`archive/diag-combined`; their wave 10 averaged 34.8 ms a frame, without the census, which these
profiles had on.)

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
- `renderObjects` 0x821DD1C0 sets the active visitor's fields (at SceneManager +0x433C:
  `autoLights` +0x0C, `manualLightList` +0x10, `scissoring` +0x14; upstream's
  `transparentShadowCastersMode` is not written here, a Runic difference to confirm) and tail-calls
  `QueuedRenderableCollection::acceptVisitor` 0x821C08B8. That function is upstream's, with the
  descending and ascending walks inlined over the sorted `std::vector<RenderablePass>` at
  +0x18/+0x1C (8 bytes an entry) and the pass-grouped walk in `0x821CC700`. The sort above only
  acts on collections whose organisation mode has the descending bit (0x2).
- **Runic's pass-grouped walk** (`0x821CC700`; in the profiles it takes 5.2 of the 9.1 ms under
  `acceptVisitor` in wave 1 of `fight5`, the sorted walks the rest) is not upstream's. For each pass
  of the map (node key +0x0C, the RenderableList* at +0x10) it copies the renderables into a static
  vector (0x83582B5C), sorts them with MSVC's `std::sort` (`0x823497E0`, an introsort, not stable)
  by a float at Renderable +0x1C, ascending (comparator `0x82190520`), and walks them:
  `visit(Pass*)` (so `_setPass` again) every time that float differs from the previous one's,
  starting from -1.0 (the constant at 0x820AA334), then `visit(Renderable*)`. A false from the pass
  visit ends the group. Renderables whose float is exactly -1.0 at the head of a group are drawn
  with the visitor's `mUsedPass` (+0x04) as the previous group left it. What Runic stores at +0x1C
  is to be read in stage 0. The native path calls the guest's own `std::sort` on the same copy, so
  the order is the guest's, and keeps the -1.0 start and the pass re-visit.
- The visitor (`SceneMgrQueuedRenderableVisitor`; the overloads sit in reverse order,
  `ogre_layout.h`): slot 1 `0x821CCB88` = visit(Renderable*): `validateRenderableForRendering`
  (SceneManager vt+0x10), then `renderSingleObject` (vt+0x54); slot 2 `0x821D27F8` =
  visit(const Pass*): `validatePassForRendering` (vt+0x0C), then `_setPass` (vt+0x398); slot 3
  `0x821C0A50` = visit(RenderablePass*): `validateRenderableForRendering`, `_setPass`,
  `renderSingleObject` (as upstream, which never validates the pass there; Runic drops upstream's
  `transparentShadowCastersMode` test, the field `renderObjects` no longer writes). In this guest
  `validateRenderableForRendering` (slot 4, `0x828AD7C0`) returns true and
  `validatePassForRendering` (slot 3, `0x821D2880`) only checks late material resolving
  (vt+0x2FC); the native path reproduces both from those bodies.
- The cut is therefore **`renderObjects`, one collection at a time**. The guest's collection sort
  stays; inside the grouped walk the native path calls the guest's `std::sort` (above). The
  native path decides per collection: when it handles every pass and every object of the
  collection it walks it itself; otherwise the whole collection goes to the guest's
  `acceptVisitor`. Falling back by whole collection keeps the guest's state caches coherent inside
  a collection; the boundary between a native collection and a guest one is handled below.
- The listeners on the render queue (`fireRenderQueueStarted`/`Ended`) are fired by `0x821BC5A0`
  around `renderQueueGroupObjects`, outside the cut; they keep running as today.

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
    guard;
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
  the per-pass and per-object fields hold the previous pass's values in comparison mode (the
  native path runs before the guest's walk) and are not set at all when it is on.

### The boundary with the guest's state

The guest's walk leaves state behind that later code reads, and its own caches assume that every
pass went through it:
- `_setPass` and `renderSingleObject` keep the last light hash, the GPU parameters' dirty flags and
  the bound programs; the D3D9 render system keeps the texture stages, the declaration and the
  pass iteration count; our session drops state that did not change since the last call.
- Side effects to keep: `_setTexture` calls Runic's `touch` (`ogre_layout.h`), which loads the
  texture and notifies its manager; `renderSingleObject` calls the renderable's `preRender` and
  `postRender` and any render object listener (RTSS's and others); `_render` counts faces and
  batches; the visitor's `mUsedPass` (+0x04) is read by the next grouped walk (above). To read in
  stage 0 as well, from upstream's `renderSingleObject` and `_setPass`: the culling flip for
  negatively scaled objects, the re-issue of view-dependent texture coordinate generation, and the
  camera's polygon mode override.

When a native collection is followed by a guest one, the native path writes back the
guest-visible state the guest's walk would have left (the visitor's `mUsedPass`, the
AutoParamDataSource's current pass, the render system's bound programs) and invalidates by
construction every cache that can make the guest skip a call: it sets them to values that force
the guest to send every state again (the GPU parameters all dirty, the last light hash to a value
no pass has, our session's memo cleared). Stage 0 lists those fields and readers by reading the
code. Two checks: comparison mode compares the written-back values with what the guest's walk
leaves; and a forced-resend mode (diagnostics build) runs the native path on with every cache
invalidated before every guest collection, so the guest collections' per-draw state can be
compared between the normal and the forced-resend runs of the same capture frames. A wrong
invalidation shows as a guest draw whose effective state differs.

### Bit identity

Comparison mode asks for every constant bit for bit. The guest computes its derived matrices
(world-view-projection and the like) with recompiled PowerPC arithmetic, fused multiply-adds
included, and the order of operations decides the bits (docs/game-thread-parallelism.md). What
must be matched is what ReXGlue emitted for those instructions, not the PowerPC semantics: for
example `fmadds` becomes a double fused multiply-add rounded to float (two roundings), which
`fmaf` would not reproduce, and VMX code flushes denormals. The native code copies, for each
automatic constant it supports, the operations the recompiled `_updateAutoParams` 0x82201EB0 and
the AutoParamDataSource getters perform, in their order, in a translation unit built without
floating point contraction, with no tolerance. It must give the same bits on x86-64 (with and
without hardware FMA), arm64 and MSVC, so comparison mode runs on the desktop and on the POCO. A
constant type that cannot be made identical stays unsupported, and passes that use it fall back.

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
draw the same effective state ("What equivalent means", below):
- `SetRenderState` and the other state commands;
- `SetTexture` and the sampler states;
- the vertex declaration and buffer bindings;
- `SetConstants` with the same ranges;
- `Draw`.

The resources (buffers, textures, programs) are the ones the capture already announces and
mirrors by guest identity. The native path refers to them through the same registry and does not
create any of its own.

## 2. Stages

Each stage is behind a runtime option (below), falls back to the guest for everything it does
not handle, and is integrated only after its verification passes on the replays and on the
device. The gains are estimates from the profiles above, to be measured per stage. Stage 0
measures the share of draws each stage covers; until then the split between stages 1 to 3 is a
guess, and their sum is bounded by the main scene's walk (draw path plus capture: 6.0 ms in
`fight.txt`, 11.0 ms in wave 10 of `fight5`). The ranges assume the covered draws lose all of
their guest and capture cost except the native walk's own, which is guessed at 3-5 us per draw
(it includes the automatic parameters, on which the guest spends about 2.7 us a draw; a guess until
stage 1 measures it). The upper ends need full coverage and about 3 us a draw.

| Stage | What | Covers (to measure) | Estimated gain, `fight.txt` / `fight5` wave 10 | Risk |
| --- | --- | --- | --- | --- |
| 0 | `guest_abi` entries for the queue, the collection and the visitor, with evidence; the boundary list (state written back, caches invalidated, side effects); count draws per collection, renderable class, pass kind and program; build the comparison harness (section 3) with an empty native path (every collection falls back) | - | 0 (the harness's cost, off by default) | Low |
| 1 | Whole solids-basic collections of the main scene (the render target is the window, not a render texture) whose every pass and object qualifies, each criterion read from guest data: one pass iteration and no iterate-per-light, light scissoring and light clip planes off on the pass; vertex and fragment programs whose automatic constants are all in a supported list (fixed in stage 0 from the census); an RTSS SGPass whose sub-render states are all in a supported list (fixed in stage 0: every pass of this guest has one, `docs/game-thread-parallelism.md` step 4), done natively for those states; renderables whose vtable is one of the known classes listed in `guest_abi` (SubEntity, StaticGeometry's GeometryBucket); for a SubEntity, an entity with no skeleton and no vertex (morph or pose) animation, so its render operation is the submesh's own data; no render object listener other than RTSS's; a pass depth bias of zero; no identity view or projection on the renderable; Runic's grouped walk reproduced (the guest's sort, the +0x1C re-visit) | measured in stage 0 | 2-2.5 ms / 3.5-4.5 ms | Medium-high |
| 2 | Skinned entities: bone matrices as `cacheBoneMatrices` left them, the same palette upload | the characters, the dead included | +1-1.5 ms / +2-3 ms | Medium |
| 3 | Transparent passes in their sorted order, the RTSS sub-render states not supported in stage 1, billboards and particle renderers | effects, decals, UI in the world | +0.5-1 ms / +1-1.5 ms | Medium-high |
| 4 | The projector passes (shadow and light textures, their own render targets and cameras) | the 2 projector passes | +1.5 ms / +1.5 ms (their walk: 2.2 / 2.1 ms, less a native walk taken as 0.7 ms, more than the other rows assume) | Medium-high |
| 5 | Culling and the scene graph update natively (the octree walk, `_updateSceneGraph`), feeding the native walk directly | culling, graph | up to the culling, 2.9 ms / 4.5 ms, less the native culling's cost; the graph update not measured apart | High |

Stages 1 to 4 together aim at the 8.2 ms of the walk: around 5-6.5 ms of it back in `fight.txt`, and
8-10.5 ms of the 13.2 ms in wave 10 of `fight5`. Stage 5 is a separate decision after them (it is
the culling candidate C of the 2026-10-13 study). None of them touches the game's behaviour: the
dead, the loot and every object keep being updated and drawn as now. Only the cost of drawing each
one goes down.

## 3. Verification and the A/B switch

**Comparison mode** (a `--diag_` option, off by default, in the diagnostics build):
- In the same frame, the native path runs first and records what it would emit for each pass and
  object, without emitting it. Then the guest path runs as today and the capture records what it
  emits.
- The two streams are compared per draw: target, viewport, every render state, every texture
  binding with its sampler state, the vertex declaration and bindings, the index range, every
  constant range bit for bit, and the draw parameters.
- The first difference of a frame is logged with the pass, the renderable's class and address,
  and the field that differs. A per-run counter reports how many draws matched, differed or fell
  back, by reason.
- The write-back check (section 1, "The boundary with the guest's state") runs in the same mode.
- Comparison mode runs both walks and the diff, so its frame times say nothing about the gain.
- A texture first used in a frame is loaded by `touch` inside the guest's walk, after the native
  path ran: the comparison counts those draws apart (a texture not yet loaded), not as
  differences.
- It runs on the desktop and on the POCO (bit identity across compilers and CPUs, section 1).
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
  AutoParamDataSource's or the pass's state after a native collection, expecting what the guest's
  walk left there: the next guest collection inside the same frame as much as code after the walk.
  Stage 0 lists the readers; the native path writes back or invalidates (section 1), and comparison
  mode checks the write-back.
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
  in place, and a collection whose objects have a listener that does something unknown falls
  back. The game's own
  listener draws the GUI from `renderQueueStarted` (above); its draws reach our capture as today.
- **Maintenance:** a second implementation of OGRE 1.7's draw path, kept equal by the comparison
  mode, which stays in the diagnostics build for as long as the native path exists.
- **The previous attempt:** the legacy port tried a detached OGRE. This plan does not reuse its
  architecture (CLAUDE.md). Facts from it are checked again in the code before they are used.

## What is needed to start stage 0

- The disassembly of `renderObjects` 0x821DD1C0, `acceptVisitor` 0x821C08B8 and its grouped
  walk 0x821CC700, the visitor's three slots and `_setPass`/`renderSingleObject`, read against
  OgreSceneManager.cpp 1.7.0; `guest_abi` entries for the queue's structures, with evidence.
- The boundary list of section 1: what the guest's walk leaves that later code reads, and its
  caches.
- The comparison harness, with the native path empty (everything falls back, every draw counted
  as "not handled"), measured with the option off to show it costs nothing.
- A draw census per frame, by renderable class, pass kind and program, from a diagnostics build
  on the POCO in `fight.txt` and `fight5.txt`, to size stages 1 to 4 before writing them.
