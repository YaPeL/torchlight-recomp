# Native render queue: investigation record

Status: closed as a project on 2026-10-23; the native render queue is paused. This records what
was measured and learned, and the owner's decision: the next work on the guest thread's cost is
(2) a cheaper capture and cheaper hooks (our side of each D3D9 call) and (3) the culling, with
only its pure visibility test native. The native render queue (option B below) may be resumed
later, one part at a time, on the terms of section 4.

## What was measured

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
the GUI, 1.8 -> 6.0 ms. `0x821BC468` is `fireRenderQueueStarted`: it loops over the scene manager's
render queue listeners (the vector at +0x42B8/+0x42BC) and calls each one's slot 3,
`renderQueueStarted(id, invocation, skip)`. The game's listener `0x821FBC60` draws CEGUI there: the
window tree (`0x821AFEA0`, recursive; the windows' own drawing `0x821AFF88` and `0x821B01B8`) and
the GUI renderer's draws (`0x821D2F80`, straight to `_render`). Its draws do not go through the
render queue, so none of the designs below touches them; why the GUI grows with the dead and the
loot is still open. The report of 2026-10-22 had named this row "firePreFindVisibleObjects" and "the
scene graph update"; both were wrong (`0x821BC5A0`, which it was named after, holds the render queue
walk too).

## 1. The walk, as Runic's build does it

From the disassembly, against OgreSceneManager.cpp and OgreRenderQueueSortingGrouping.cpp 1.7.0
(`guest_abi/render_queue.h`, with evidence for each entry):
- `0x821DD098` (OctreeSceneManager vtable 0x82005B9C slot 26) is `renderBasicQueueGroupObjects`.
  For each priority group it sorts the six collections (`QueuedRenderableCollection::sort`
  0x821DCFC8: radix sort above 2000 entries, stable sort below, only with the descending bit) and
  draws solids basic, transparents unsorted and transparents (always descending) through
  `renderObjects` (slot 27, `0x821DD1C0`).
- `renderObjects` sets the active visitor's fields (SceneManager +0x433C: `autoLights` +0x0C,
  `manualLightList` +0x10, `scissoring` +0x14) and tail-calls `acceptVisitor` 0x821C08B8.
  **Runic difference:** upstream's `transparentShadowCastersMode` is gone from the visitor, and so
  is its test in `visit(RenderablePass*)`.
- `acceptVisitor` is upstream's, with the sorted walks inlined; an organisation mode not notified
  falls back to the grouped walk, else to the backward walk for either sort bit.
- **Runic difference:** the pass-grouped walk `0x821CC700` copies each group's renderables into a
  static vector (0x83582B5C), sorts them with MSVC's `std::sort` (not stable) by a float at
  Renderable +0x1C, and visits the pass again (`_setPass`) every time that float changes, starting
  from -1.0 (0x820AA334); a head run at -1.0 is drawn with the visitor's `mUsedPass`.
- The visitor: slot 1 `0x821CCB88` validates the renderable and calls `renderSingleObject` (slot
  21, `0x821C5FA0`); slot 2 `0x821D27F8` validates the pass and calls `_setPass` (slot 230,
  `0x821C5730`); slot 3 `0x821C0A50` does both. In this guest `validateRenderableForRendering`
  (slot 4) returns true and `validatePassForRendering` (slot 3) only checks late material
  resolving.
- **Runic difference:** `SceneManager::bindGpuProgram` (slot 40, `0x821C14B8`), which `_setPass`
  calls for each program, resets the light hash (+0x43B0), sets the AutoParamDataSource's light
  list to null and marks every GPU parameter dirty (+0x43B4, a u16) before binding.
- **Runic difference:** `SubEntity::getRenderOperation` 0x821A3DE8 copies the submesh's own index
  data and never reads the entity's LOD index.
- The render queue listeners (`fireRenderQueueStarted`/`Ended`, the GUI's included) are fired by
  `0x821BC5A0` (`renderVisibleObjectsDefaultSequence`), outside the walk.

## 2. Stage 0: how much a native path could take

A diagnostic (`--diag_queue_census`, only in builds with `TORCHLIGHT_DIAGNOSTICS`, PR #47)
classifies every collection `renderObjects` draws, per pass group and per entry, against the
criteria guest data decides (main scene, solids basic, vertex and fragment programs, one
iteration, no per-light iteration, no light scissoring or clip planes, no depth bias, SubEntity or
StaticGeometry GeometryBucket, not skinned), and times the guest's walk of each collection. POCO
X7, 2026-10-23, commit 79baf91:

| Window | Queue draws a frame | Walk a frame | Native, fallback by whole collection | Native, fallback by pass group |
| --- | --- | --- | --- | --- |
| `fight.txt` | 270 | 8.1 ms | 21.8 % of draws / 20.6 % of the walk | 30.4 % / 29.6 % |
| `fight5` wave 1 | 267 | 7.5 ms | 22.1 % / 21.0 % | 31.9 % / 31.4 % |
| `fight5` wave 10 | 531 | 12.0 ms | 11.1 % / 13.2 % | 42.5 % / 37.4 % |

No pass group ever mixed objects a native path would handle with objects it would not (a pass
belongs to one material, and static and skinned objects never share one); the collections that mix
(1.4 %) are the large ones of the main scene. No group started its walk at -1.0. The rest of the
draws in `fight.txt`: projector passes 28.7 %, transparents 25.4 %, skinned 15.5 %. The shares are
upper bounds for the criteria checked (the supported automatic constants, RTSS states and texture
unit kinds are not decided), and the walk's time is prorated by draws inside each collection.

## 3. Option B and its ceiling

The design the review rounds converged on: the guest keeps the walk and every `_setPass`; for each
object it handles, the native path does the per-object work of `renderSingleObject` (the automatic
constants and their upload, the RTSS per-object listener, the draw and its capture). The per-object
work is 70.6 % of the walk in `fight.txt` (automatic parameters 0.86, constants upload 1.13, the
draw guard and its capture 1.40, RTSS per object 0.61, vertex buffers 0.53, the rest of
`renderSingleObject` 1.25 of 8.19 ms) and 73.3 % in wave 10 of `fight5`. Split by draw type with the
census's shares, less a guessed 3-5 us a native draw:

| Draw type | `fight.txt`: per-object ms / gain | wave 10: per-object ms / gain | What it needs reproduced |
| --- | --- | --- | --- |
| Static opaque, main scene | 1.75 / 1.3-1.5 | 3.74 / 2.6-3.1 | the automatic constants, RTSS per-object states, the draw |
| Projector passes | 1.65 / 1.3-1.4 | 1.48 / 1.0-1.2 | the same, with the projector's camera and target |
| Transparents | 1.46 / 1.1-1.25 | 3.09 / 2.2-2.5 | billboard and particle renderers (dynamic buffers the guest fills) |
| Skinned | 0.89 / 0.7-0.8 | 0.50 / 0.35-0.4 | the bone palette, hardware skinning's RTSS state |
| **All** | **5.74 / 4.4-4.9** | **8.82 / 6.2-7.2** | |

`fight.txt`'s guest frame is about 22 ms: all four types together would leave about 17 ms, short of
16.6 ms (60 fps), so even complete, option B would need a second front.

## 4. Why it was paused: the guest's hidden state writes

Nine clean reviews of successive designs (2026-10-23) found the same kind of hole each time: every
guest function the native path replaces writes guest state that is invisible from its header and
that later guest code reads. What they found, by design:
- **A native walk and native passes** (the first designs): the guest's caches above our hooks
  (light hash, dirty parameter mask, bound-program flags, RS +0x294 first disabled texture unit, RS
  +0x74 cull mode, `mPassCullingMode`, the pass number, the texture projectors) describe the last
  guest pass, not the native one; restoring the guest-believed state, invalidating caches and
  writing back fields each left a case uncovered, and the same-frame comparison mode cannot see
  any of them because the guest walks everything there.
- **Native `renderSingleObject`:** the dirty mask decides which ranges the first object after a
  pass visit uploads (global and manual included); the light hash of an empty list is 0; the cull
  flip restore, view-relative texture coordinate generation, camera-relative rendering, dirty clip
  planes and the guard's buffer uploads all carry state between objects.
- **Native constants only** (`updateGpuProgramParameters`): the guest's `bindGpuProgramParameters`
  (slot 91, `0x821C2118`) copies shared parameter sets into the buffer and assigns the render
  system's active parameter pointers (+0x3C, +0x5C, reference counted); `_updateAutoParams` writes
  the parameters' active pass iteration index (+100); the AutoParamDataSource getters fill lazy
  caches.

Each hole is fixable by reading the replaced function completely and calling the guest's own code
for every state write, but the reading has to be exhaustive per part, and each review round found
more. The owner paused option B on 2026-10-23. To resume it: one part at a time, each preceded by
a complete reading of the function it replaces, with the guest's functions doing every state
write and only a computation replaced.

## 5. The decision: our side first, then the culling's pure test

**(2) A cheaper capture and cheaper hooks.** Our side of the walk is 2.96 ms in `fight.txt` and
4.65 ms in wave 10 (table above): it replaces no guest code, so no guest state is involved, and it
is checked by replays and by comparing the commands it produces. Its own plan is written
separately, measure by measure.

**(3) The culling, with only the visibility test native.** The octree walk under
`_findVisibleObjects` (0x821A6070 -> 0x821C7158), by direct child (inclusive ms a frame):

| Part | `fight.txt` | wave 1 | wave 10 | Writes guest state |
| --- | --- | --- | --- | --- |
| `Entity::_updateRenderQueue` 0x821E0D18 (with the animation it triggers) | 1.36 | 1.37 | 2.02 | yes |
| ParticleUniverse `_updateRenderQueue` 0x821BE5A8 | 0.88 | 1.04 | 1.77 | yes |
| The walk's own loop | 0.52 | 0.52 | 1.04 | reads the octree, queues nodes |
| Box against frustum (`Frustum::isVisible` 0x821C74A0), already host code | 0.44 | 0.41 | 0.75 | no |
| StaticGeometry::Region `_updateRenderQueue` 0x821CCE90 | 0.24 | 0.21 | 0.24 | yes |
| Octant test (`OctreeCamera::getVisibility` 0x821C0E78) | 0.15 | 0.19 | 0.12 | no |
| Other per-object virtuals | 0.21 | 0.24 | 0.37 | yes |
| **Total** (the 2.9 ms of the "where the walk sits" table, with the particles) | **3.80** | **3.98** | **6.32** | |

The pure visibility tests are about 0.6 ms in `fight.txt`, and the box test, the larger one, has
run as host code since the 2026-10-13 study (`--native_frustum_test`, on by default,
`hooks/frustum_test.h`, bit for bit). What is left of the pure part is the octant test (0.15 ms);
the rest of the culling writes guest state (the render queue, the animation, the particle
renderers' buffers, the per-object camera and LOD notifications) and stays the guest's.

## 6. Outside the cut, for later

The GUI is drawn by the game's render queue listener (`fireRenderQueueStarted` 0x821BC468 ->
`0x821FBC60`, CEGUI's window tree and renderer), outside the walk, and it is what grows most between
wave 1 and wave 10 of `fight5` (1.8 -> 6.0 ms). From the code: every item on the floor has one
CEGUI label window (`ceguiWidget/ItemTextButton`, created the first time it is shown, kept in the
item at +0x198); hidden labels are removed from the window tree, so they are neither walked nor
drawn; shown labels (items within a radius of the player, when the item names setting or key ask
for them) are repositioned every frame, and a shown label outside the screen is clamped to its edge
rather than dropped. How many labels are shown per wave is not measured. Work on the loot labels
and the GUI comes later.
