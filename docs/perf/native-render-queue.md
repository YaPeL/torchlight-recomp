# Native render queue: investigation record

Status: closed as a project on 2026-10-23; the native render queue is paused. This records what
was measured and learned, and the owner's decision: the next work on the guest thread's cost is
a cheaper capture and cheaper hooks (our side of each D3D9 call), then the culling with only its
pure visibility test native (section 5). The native render queue (option B below) may be resumed
later, one part at a time, on the terms of section 4.

## What was measured

Measured on the POCO X7 on 2026-10-23 with simpleperf and our symbols, binary 47a1d12 (in `develop`;
the object census, `--diag_object_census`, in the build, off in `fight.txt`, on in `fight5`), fps
cap 0. Each profile's window was checked against the fight in the run's log
(`tools/profile_window`): the profiles of 2026-10-22 had been taken partly at rest
(docs/regressions.md, "Profiles of the fight taken on the rest mark") and are replaced by these.
Milliseconds are the share of the guest thread's samples times its mean frame in the window.

On the POCO X7 the game's thread is the limit. Walking OGRE's render queue costs 8.2 ms of its 22.1
ms frame in the 30-second fight (`fight.txt`, 25 s profiled from 2.6 s after the mark, about 300
draws a frame by the frame counter, the GUI's included; the stage 0 census counts 270 in the queue):
about 27 us a draw, 17 of them in the guest's embedded OGRE (passes, automatic parameters,
constants, texture units, the per-object work) and 10 in our hooks and capture of each D3D9 call
(`ReadConstants`, the session's state checks, the resource registry, vertex buffer reads). In the
5-minute fight (`fight5.txt`, 11 s profiled in the middle of wave 1 and at the end of wave 10) the
same walk is 9.3 ms of a 25.2 ms frame in wave 1 and 13.2 ms of 39.5 ms in wave 10. The dead and the
loot are never removed in that fight, so the draws grow from about 300 to about 689 a frame by wave
10: about 19 us a draw there (frame-counter draws, the GUI's included; over the 531 queue draws the
census counts, about 25 us, and about 30 us in `fight.txt` over its 270). (The draws a frame are the
frame counter's, from the twelve `fight5` runs of 2026-10-22 on binary 03ab9ae, tag
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
the GUI, 1.8 -> 6.0 ms. `0x821BC468` is `fireRenderQueueStarted`: it loops over the scene manager's
render queue listeners (the vector at +0x42B8/+0x42BC) and calls each one's slot 3,
`renderQueueStarted(id, invocation, skip)`. The game's listener `0x821FBC60` draws CEGUI there: the
window tree (`0x821AFEA0`, recursive; the windows' own drawing `0x821AFF88` and `0x821B01B8`) and
the GUI renderer's draws (`0x821D2F80`, straight to `_render`). Its draws do not go through the
render queue, so none of the designs below touches them; why the GUI grows with the dead and the
loot is still open. The report of 2026-10-22 had named this row `firePreFindVisibleObjects` (its
item 3: "game listeners"): the listeners part held, the name did not (`0x821BC5A0`, the slot it was
named after by position, is `renderVisibleObjectsDefaultSequence` and holds the render queue walk
too).

## 1. The walk, as Runic's build does it

From the disassembly, against OgreSceneManager.cpp and OgreRenderQueueSortingGrouping.cpp 1.7.0
(`src/guest_abi/render_queue.h`, PR #47 at c64cf94, with evidence for each entry):
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
- The visitor: its slot 1 `0x821CCB88` validates the renderable and calls `renderSingleObject`
  (SceneManager slot 21, `0x821C5FA0`); its slot 2 `0x821D27F8` validates the pass and calls
  `_setPass` (SceneManager slot 230, `0x821C5730`); its slot 3 `0x821C0A50` validates the
  renderable only (as upstream) and calls both. In this guest the SceneManager's
  `validateRenderableForRendering` (its slot 4) returns true and `validatePassForRendering` (its
  slot 3) only checks late material resolving.
- `SceneManager::bindGpuProgram` (slot 40, `0x821C14B8`), which `_setPass` calls for each
  program: like upstream it resets the light hash (+0x43B0) and marks every GPU parameter dirty
  (+0x43B4, a u16); **Runic difference:** it also sets the AutoParamDataSource's light list to null.
- **Runic difference:** `SubEntity::getRenderOperation` 0x821A3DE8 copies the submesh's own index
  data and never reads the entity's LOD index.
- The render queue listeners (`fireRenderQueueStarted`/`Ended`, the GUI's included) are fired by
  `0x821BC5A0` (`renderVisibleObjectsDefaultSequence`), outside the walk.

## 2. Stage 0: how much a native path could take

A diagnostic, the queue census (`--diag_queue_census`, only in builds with `TORCHLIGHT_DIAGNOSTICS`;
`src/hooks/queue_census*`, PR #47 at c64cf94), classifies every collection `renderObjects` draws,
per pass group and per entry, against the criteria guest data decides (main scene, solids basic,
vertex and fragment programs, one iteration, no per-light iteration, no light scissoring or clip
planes, no depth bias, SubEntity or StaticGeometry GeometryBucket, not skinned), and times the
guest's walk of each collection (`renderObjects` only, so its walk excludes the sorts and the rest
of `renderBasicQueueGroupObjects`, and it is lower than the profile's). POCO X7, 2026-10-23, commit
79baf91 (the review fixes of c64cf94 change neither the modes these runs met nor the counts):

| Window | Queue draws a frame | Walk a frame | Native, fallback by whole collection | Native, fallback by pass group |
| --- | --- | --- | --- | --- |
| `fight.txt` | 270 | 8.1 ms | 21.8 % of draws / 20.6 % of the walk | 30.4 % / 29.6 % |
| `fight5` wave 1 | 267 | 7.5 ms | 22.1 % / 21.0 % | 31.9 % / 31.4 % |
| `fight5` wave 10 | 531 | 12.0 ms | 11.1 % / 13.2 % | 42.5 % / 37.4 % |

No pass group ever mixed objects a native path would handle with objects it would not (a pass
belongs to one material, and static and skinned objects never share one); the collections that mix
(1.4 % of the collections) are the large ones of the main scene. No group started its walk at -1.0.
The rest of the draws: projector passes 28.7 %, transparents 25.4 %, skinned 15.5 % in `fight.txt`;
16.8 %, 35.1 % and 5.7 % in wave 10. The shares are upper bounds for the criteria checked (the
supported automatic constants, RTSS states and texture unit kinds are not decided), and the walk's
time is prorated by draws inside each collection.

## 3. Option B and its ceiling

The design the review rounds converged on: the guest keeps the walk and every `_setPass`; for each
object it handles, the native path does the per-object work of `renderSingleObject` (the automatic
constants and their upload, the RTSS per-object listener, the draw and its capture). The per-object
work, from the profile (table 1, `fight.txt`): automatic parameters 0.86, constants upload 1.13, the
draw guard and its capture 1.40, RTSS per object 0.61, vertex buffers 0.53, the rest of
`renderSingleObject` 1.25: 5.78 of the walk's 8.19 ms (70.6 %); in wave 10 of `fight5`, 9.64 of
13.15 ms (73.3 %). Split by draw type with the queue census's shares of the draws (not of the time;
the census's own walk excludes part of the profile's, so it is not used as the base), less a
guessed 3-5 us a native draw over its 270 / 531 queue draws:

| Draw type | `fight.txt`: per-object ms / gain | wave 10: per-object ms / gain | What it needs reproduced |
| --- | --- | --- | --- |
| Static opaque, main scene | 1.76 / 1.35-1.5 | 4.10 / 3.0-3.4 | the automatic constants, RTSS per-object states, the draw |
| Projector passes | 1.66 / 1.25-1.45 | 1.62 / 1.15-1.35 | the same, with the projector's camera and target |
| Transparents | 1.47 / 1.15-1.25 | 3.38 / 2.45-2.8 | billboard and particle renderers (dynamic buffers the guest fills) |
| Skinned | 0.90 / 0.7-0.75 | 0.55 / 0.4-0.45 | the bone palette, hardware skinning's RTSS state |
| **All** | **5.78 / 4.4-5.0** | **9.64 / 7.0-8.05** | |

`fight.txt`'s guest frame is 22.1 ms: all four types together would leave 17.1-17.7 ms, short of
16.6 ms (60 fps), so even complete, option B would need a second front.

## 4. Why it was paused: the guest's hidden state writes

Nine clean reviews of successive designs (2026-10-23) found the same kind of hole each time: every
guest function the native path replaces writes guest state that is invisible from its header and
that later guest code reads. What they found, by design:
- **A native walk and native passes** (the first designs): the guest's caches above our hooks
  (light hash, dirty parameter mask, bound-program flags, RS +0x294 first disabled texture unit, RS
  +0x74 cull mode, `mPassCullingMode`, the pass number, the texture projectors) describe the last
  guest pass, not the native one; restoring the guest-believed state, invalidating caches and
  writing back fields each left a case uncovered, and the verification the designs relied on (a
  comparison mode that, in the same frame, records what the native path would emit and then runs
  the guest's path) cannot see any of them, because the guest walks everything there.
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
a complete reading of the function it replaces and of everything it calls, virtual calls
included, with the guest's functions doing every state write and only a computation replaced;
each part checked in the same frame both ways and, for what that cannot see, by comparing the
guest's words the part must leave (listed from the reading) after the guest's own part.

**Resumed on 2026-10-30 (the owner's decision), with the hidden state attacked at the root.** The
goal: with the fps cap at 30, wave 10 of `fight5` is about 37 ms a frame on the POCO and must come
under 33.3 ms at its p95, about 5 ms.
- *A store trace.* Every scalar guest store of the generated code goes through
  `REX_STORE_U8/16/32/64` (`generated/default/torchlight_pch.h:146-149`), the vector, `dcbz` and
  atomic ones through `REX_RAW_ADDR`. A diagnostics build (`TORCHLIGHT_DIAGNOSTICS`) adds a header
  to the generated code's target that redefines them (nothing generated or in the SDK edited):
  outside a traced call one load and a predicted branch per store; inside the call tree of a traced
  entry (wrappers on renderSingleObject and `_setPass`; `_updateAutoParams` and
  `bindGpuProgramParameters` run inside renderSingleObject's tree) each write is recorded with its address, size, old and new value;
  `REX_RAW_ADDR` lines are snapshot on first touch and diffed; our own `abi::Write*` record too;
  kernel exports are logged as events.
- *The criterion per call* (`--diag_store_compare`, built 2026-10-31). Each `renderSingleObject`
  runs twice from the same state. First the candidate (the native path; the guest's own function
  until there is one), with every guest write recorded with the bytes it replaced (the generated
  code's stores, memory-mapped stores outside the device registers and raw lines, the critical
  sections' words, our hooks' writes through `abi::WriteBytes`, the host's guest copies and
  pixel packing) and its commands kept apart: the capture session's comparison log diverts them, so
  nothing reaches the shadow, the early out's memos, the constant mirror or the streams (resource
  descriptions still go out: they are idempotent). Its writes are undone newest first and the
  context put back; then the guest's function runs as usual, recorded the same way. Plain command
  bytes cannot be compared, because the run kept deduplicates against host state the first run did
  not touch, so the criterion is what each draw sees. The two runs must leave the same bytes in
  every address either wrote (the stack below the call excluded), the same stack pointer and return
  registers (the generated code keeps the non-volatile ones in locals), and the same events, whole constant uploads and draws in order, each
  draw with the same value for every state the call set. Each call is thus checked against all the
  memory it touches, the hidden caches included (the light hash, the dirty mask +0x43B4, the pass
  iteration index, the auto parameters' lazy caches). Each difference is logged as byte runs with
  the function that last wrote them in each run ("none" in a run that did not write them points to
  another thread). Raw lines (vector stores, atomics, dcbz) are undone and compared only in the
  bytes the call wrote: the 16-byte granules it accessed, and from a line's start (dcbz) the bytes
  that turned to zero. A call is not verified, and counted apart by reason and site, when the
  candidate does what the undo cannot take back (a kernel call other than the critical sections
  and a few that only read, a store to a device register), or when another thread touches what
  either run touched (the render thread enters every granule its runs touch into a table tagged
  with the call; every other thread looks its stores, raw accesses and critical sections up
  there). A candidate in either case is not undone: its memory and context stay and its diverted
  commands are replayed into the session (the owner's decision, 2026-10-31: past 0.1 % of the
  calls, or all at one site, the line stops). A write of another thread between the last check
  and the undo is not seen (the residual race). The host side of the capture outside the commands
  (content versions, unresolved counts) is not undone.
  Checked on the desktop, offscreen: with the guest's function as the candidate,
  `fight.txt` compares 243965 calls and the ten waves of `fight5` 1542306, with no difference
  (127916 in `fight.txt` with the final race detection, none unverified); a candidate that flips one bit of the
  SceneManager, changes r31 and adds a command every 1000 calls is caught on exactly those calls
  (41 of 41151). The guest frame is 3-4x slower while comparing (44 ms at rest, 68 ms in the
  fight).
- *Stages*, by gain over risk (realistic ms, `fight.txt` / wave 10): 1 static opaque of the main
  scene (1.0 / 2.0-2.8), 2 projector passes (0.9 / 0.8-1.1), 3 transparents (0.7 / 1.5-2.2), 4
  skinned (0.3, last or never); `_updateAutoParams` and `bindGpuProgramParameters` within stage 1,
  `_setPass` last. Stages 1-3 probably reach the goal, with little margin.
- *Before each stage is integrated:* no difference in the comparison over runs of `fight.txt` and
  `fight5`, and an A B B A of the guest's work per frame with Even frames off and the thermal state
  logged. After stage 1, with its real numbers, the owner decides on stage 2.

### Stage 1 so far (2026-10-31, branch perf/native-render-object)

- **P0** (hooks/native_render_object.cpp, `--native_render_object`, off): renderSingleObject's
  glue on the host, every callee the guest's, called by vtable in the same order. Every object of
  every class takes it after the first light iteration (the guest builds its static light list
  then).
- **P1a**: `_updateAutoParams` on the host for the matrix constants (the guest's getters, the copy
  and transpose on the host) and ACT_DERIVED_SCENE_COLOUR, which this game sets per object: every
  call of the fight on the host.
- **The XDK vector copy** on the host (`--native_vector_copy`, off, its own check).
- **Checked**: the per-call comparison over fight.txt and the ten waves of fight5 with 0
  differences and 0 unverified calls at every step (for P1a only since the reference run was made
  the guest's code all the way down: before, it reached the native `_updateAutoParams` through its
  symbol too); the copy check over 115 million copies.
- **Measured** on the POCO, fight.txt, A B B A with the flag on one APK (the phone ran about 25 %
  slower than the day before, so only differences within a series count):

| Build | Fight off (ms) | Fight on (ms) |
| --- | --- | --- |
| P0, no PGO | 26.59 | 25.06 (-0.9) |
| P0, PGO, linear target search | 20.36 | 21.62 (+1.26) |
| P0 + P1a, PGO, direct-mapped targets | 23.12 | 22.71 (-0.4, within the runs' spread) |

- **Profile** (simpleperf, the native path on, fight): the hook is 9.3 % of the samples, its own
  code 4 % of that. Of the native path: `_render` 38 % (the guest's 60 %: vertex buffer binding
  47 %, declaration 15 %; our CaptureDraw 37 %), `updateGpuProgramParameters` 32 %
  (`bindGpuProgramParameters` 68 %, of which our ReadConstants 70 %; `_updateAutoParams` 25 %),
  the RTSS listener 12 %. What is left is mostly our own capture (ReadConstants, CaptureDraw) and
  the guest's buffer bindings, not the per-object glue the stage replaced.

## 5. The decision: our side first, then the culling's pure test

**First: a cheaper capture and cheaper hooks.** Our side of the walk is 2.96 ms in `fight.txt` and
4.65 ms in wave 10 (table 1): it changes none of the guest code that runs, so no guest state is
involved, and it is checked by replays and by comparing the commands it produces. The measures
proposed (savings estimated, not measured, in `fight.txt` / wave 10) and the owner's decisions of
2026-10-28:

| Measure | What it caches | Estimate (ms) | Decision |
| --- | --- | --- | --- |
| M4 | the session's memo by direct index; no command built when the arguments did not change | 0.15-0.25 / 0.25-0.35 | first |
| M3 | the captures of vertex declarations, programs and textures, by pointer and generation | 0.2-0.3 / 0.3-0.4 | second |
| M1 | each parameter object's flattened ranges and automatic constants, compared without byteswap | 0.35-0.45 / 0.55-0.7 | only with a reliable version signal of the guest's parameters |
| M2 | `CaptureDraw`'s binding map read, registry lookup and target flip, per object and version | 0.25-0.35 / 0.45-0.6 | dropped |

Each one in its own change, with a diagnostic that runs the old and the new capture on the same
call and compares the commands byte by byte, and an A B B A on the POCO. M2 is dropped for its
risk: OGRE 1.7's `VertexBufferBinding` has no version counter, so a cache needs every change to the
binding map seen (`setBinding`, `unsetBinding`, `unsetAllBindings` and any write the game makes to
the map directly); one missed change draws with the wrong buffers, and the backend may fault on
them.

M1 is dropped too (2026-10-28): the guest has no version signal for a parameter object's values.
Its classes are stock OGRE 1.7 with no counter of Runic's, and every write of constant values is a
plain copy (`sub_821C28D8`). The variability mask `bindGpuProgramParameters` gets is the
SceneManager's (+0x43B4), set to per-object for every renderable and to everything after each
pass or program bind; and `getFloatPointer` lets any code store into the buffer with no call. Only
the layout (buffer size, the vectors' ends, the automatic constants' list) shows changes, which is
not enough to skip reading the values.

M3 is dropped as well (2026-10-28), the narrower version for programs only included. Estimated
again by counting the calls and what a cache would remove from each: about 0.05 ms a frame in
`fight.txt` (0.03-0.10) and 0.10 ms in wave 10 (0.05-0.20). The plan's 0.2-0.3 / 0.3-0.4 ms
assumed a full capture on every call, but textures and vertex declarations are already cached by
pointer and generation (`LiveTextureDescribed`, `LiveDeclarationBytesSent`; the declaration's byte
compare stays, it is what catches `addElement` on a declaration in use), and most of each call is
the hook's framing and `State`, which a cache keeps. Only the program capture (its name read and
the registry insert on every bind) had a real remainder, 70-80 % of the savable amount.

**Then: the culling, with only the visibility test native.** The octree walk under
`_findVisibleObjects` (0x821A6070 -> 0x821C7158), by direct child (inclusive ms a frame):

| Part | `fight.txt` | wave 1 | wave 10 | Writes guest state |
| --- | --- | --- | --- | --- |
| `Entity::_updateRenderQueue` 0x821E0D18 (with the animation it triggers) | 1.36 | 1.37 | 2.02 | yes |
| ParticleUniverse `_updateRenderQueue` 0x821BE5A8 | 0.88 | 1.04 | 1.77 | yes |
| The walk's own loop | 0.52 | 0.52 | 1.04 | reads the octree, queues nodes |
| Box against frustum (`Frustum::isVisible` 0x821C74A0), already host code | 0.44 | 0.41 | 0.75 | no |
| StaticGeometry::Region `_updateRenderQueue` 0x821CCE90 | 0.24 | 0.21 | 0.24 | yes |
| Octant test (`OctreeCamera::getVisibility` 0x821C0E78) | 0.15 | 0.19 | 0.12 | lazily: the view, the frustum and its planes |
| Other per-object virtuals | 0.21 | 0.24 | 0.37 | yes |
| **Total** (in `fight.txt`, the 2.9 ms culling row of the "where the walk sits" table plus the ParticleUniverse row) | **3.80** | **3.98** | **6.32** | |

The visibility tests are about 0.6 ms in `fight.txt`, and the box test, the larger one, has run as
host code since docs/game-thread-parallelism.md section 12 (2026-10-14; `--native_frustum_test`, on
by default, `src/hooks/frustum_test.h`, bit for bit; it makes no calls). What is left is the octant
test (0.15 ms in `fight.txt`, 0.12 ms in wave 10, inclusive), and it is not quite pure: it reads
each plane it tests (the far plane only with a far distance) through the camera's `getFrustumPlane`
(OctreeCamera slot 91, 0x821C0E50), which hands off to the cull frustum when one is set (+1224) and
otherwise (0x821C4570) runs `updateFrustumPlanes` first (vt+224; OgreFrustum.cpp:215-220), which
updates the view and the frustum when they are out of date (OgreFrustum.cpp:843-852;
`Camera::isViewOutOfDate` reads the parent node's derived transform and writes the camera's cached
copies, OgreCamera.cpp:329-354). A native version calls the guest's getter for the planes and
replaces only the arithmetic of the test, so its gain is the 0.15 / 0.12 ms less the getters' cost
(six virtual calls a test, each with its own update checks) and its own: a small fraction. The rest
of the culling writes guest state (the render queue, the animation, the particle renderers' buffers,
the per-object camera and LOD notifications) and stays the guest's.

Decided on 2026-10-27 (the owner): the native octant test is dropped. Its measured ceiling, 0.15 ms
in `fight.txt` and 0.12 ms in wave 10 before the getters' cost, is too small for a hook on code that
updates guest state lazily.

## 6. Outside the cut, for later

The GUI is drawn by the game's render queue listener (`fireRenderQueueStarted` 0x821BC468 ->
`0x821FBC60`, CEGUI's window tree and renderer), outside the walk, and it is what grows most between
wave 1 and wave 10 of `fight5` (1.8 -> 6.0 ms). From the code: every item on the floor has one
CEGUI label window (`ceguiWidget/ItemTextButton`, created the first time it is shown, kept in the
item at +0x198); hidden labels are removed from the window tree, so they are neither walked nor
drawn; shown labels (items within a radius of the player, when the item names setting or key ask
for them) are repositioned every frame, and a shown label outside the screen is clamped to its edge
rather than dropped.

Measured on 2026-10-29 (POCO X7, `fight5`, 25 s profiles in waves 1, 5 and 10; the hero attacks
standing still, the loot around them):

| | Wave 1 | Wave 5 | Wave 10 |
| --- | --- | --- | --- |
| Labels shown (glyphs) | 13 (84) | 40 (315) | 79 (658) |
| GUI pass a frame (renderGUI 0x821FBC60) | 1.63 ms | 3.29 ms | 5.32 ms |
| of it, guest code / our hooks | 1.30 / 0.33 | 2.67 / 0.62 | 4.29 / 1.03 |
| Label update (0x821A83C8) | 0.59 ms | 0.62 ms | 0.64 ms |
| GUI draws / all draws | 44 / 270 | 106 / 428 | 184 / 609 |

About 56 us a label, linear (45 in guest code, 11 in our hooks); two draws a label (its frame and
its text alternate textures). In wave 10, the window tree re-queued every frame (0x821AFEA0) is
2.49 ms and the draw pass (`doRender` 0x821D2F80) 2.56 ms, of which `initRenderStates` (about 25
render system calls before each draw) is 0.75 ms.

Batching the GUI's quads in the backend is dropped (the owner's decision, 2026-10-29): it would
save time only on the backend thread, which is not the frame's bottleneck (the guest thread is);
consecutive draws already change texture (frame, text, frame...), so batching means reordering
across labels, which overlap with the depth test always passing, so it would change which label
covers which; and it saves nothing of our capture, which is paid in the hooks on the guest thread.

**Why the GUI is re-queued every frame (2026-10-29).** CEGUI 0.6 re-queues the whole window tree
(0x821AFEA0, 2.69 ms a frame in wave 10) on any frame something set the System's redraw flag
(System singleton 0x835067CC, +40); the area, visibility and enabled setters compare with the old
value, `setText` does not (the HUD compares before calling it). A census of the writers
(`--diag_gui_redraw_census`, `hooks/gui_redraw_census_hooks.cpp`, only with
`TORCHLIGHT_DIAGNOSTICS`: the 108 guest functions that set the flag, each call recorded with three
return addresses) found, in `fight5` (the caveat below applies to these numbers):

| Re-queued frames, by the writers seen in them | Wave 1 | Wave 5 | Wave 10 |
| --- | --- | --- | --- |
| All re-queued frames | 65.8 % | 93.4 % | 92.3 % |
| Only a moving and fading window (sub_821FF5B0 -> sub_82335110: likely the floating combat text, a real change) | 41.6 % | 43.2 % | 36.1 % |
| Only the hovered unit's label disabled and enabled again (below) | 1.1 % | 27.2 % | 28.9 % |
| A text set and a child added (sub_8233D130), never alone | 0 | 0 | 0 |

- **The bouncing arrows** of panels that are not open (0x821A41A0 moves about 14 a frame, all of
  windows not drawn) set the flag every frame too. Skipping their moves (branch
  `fix/gui-hidden-arrows`, draft PR, `--native_gui_hidden_arrows`) removes them from the census, but
  in a fight the other writers keep the GUI re-queued: the GUI pass measured 1.71 / 3.79 / 5.73 ms
  without it and 1.61 / 3.16 / 5.69 ms with it (one run each, within the noise). Kept on its branch
  to be measured with whatever comes next.
- **The hovered unit's label (not fixed; the owner's decision to stop here).** Once per game update
  (sub_821E5910, called by sub_821F5CA0 @0x821F5FF0 in the gameplay state), the player's two
  targets (player+72 and player+80; items, gold, breakables and trigger units, whose virtual slot 64
  is sub_822C8010, "set highlighted") are un-highlighted (`slot64(false)` @0x821E66AC and
  @0x821E67AC) and, after the new targets are written (@0x821E6880, @0x821E6890), highlighted again
  (@0x821E68B4, @0x821E68D8). sub_822C8010 compares with unit+368 and calls sub_822C8088, which
  refreshes the unit's label window (unit+408): `setEnabled` (sub_825AE860; onEnabled 0x825B0928 and
  onDisabled 0x825B0A00 each set the flag), the "TextColour" property (white, then grey, then white
  again, through `setProperty` sub_825AB6F8 and the Falagard property 0x825E3648, which does not
  compare) and, when enabling, the window's vt+264 with 0 (sub_825AECF0, likely `moveToFront`). With
  the target unchanged the label ends every update as it started, before anything is rendered, but
  the GUI is re-queued: alone in 28.9 % of wave 10's frames, about 0.9 ms a frame at 3.2 ms a
  re-queue (about 0.8 ms in wave 5).
  The fix designed: midasm hooks at @0x821E66AC and @0x821E67AC (r3; true jumps to @0x821E66B0 and
  @0x821E67B0) record the unit and skip the un-highlight; one before @0x821E6948, where both
  branches meet (r31; the player at r31+60), calls slot 64 with false for each recorded unit that is
  no longer player+72 or player+80. Risks: (1) that hook calls a guest virtual from the host in the
  middle of the game update (`game_menu/guest_call` does it elsewhere; not checked there); (2) the
  hovered label is no longer brought to the front every update, only when first highlighted, so a
  label created later (new loot) could cover it, a visible change, unless the hook also calls
  sub_825AECF0 and that does not set the flag when the label is already in front (not checked);
  (3) between the skipped call and the deferred one, sub_822A48D0 and sub_822801D0 see the old
  target still highlighted (neither reads unit+368 directly; their callees not checked).
- **Caveat on the census (its review, 2026-10-29).** Only the first return address of each call
  chain is reliable: ReXGlue drops the `bl __savegprlr_N` prologue calls, so a frame's LR slot (-8
  from its stack pointer) can hold a stale address of an earlier frame, and the groups above were
  told apart by the second and third addresses. The census also counts calls of the writers, not
  flag sets (a few set the flag only on some paths; the label's `setEnabled` notifications are not
  among them). The hovered label's pattern itself was confirmed by reading the code, but its 28.9 %
  and ~0.9 ms are not firm: whoever picks this up should fix both points (trust a frame's LR slot
  only when its function stored LR there; record a call only when the flag goes to 1) and measure
  again first. The probe is on the branch `diag/gui-redraw-census` (#65, closed).

## 7. Ideas from Rekindled 1.0.2 (2026-10-24)

Rekindled 1.0.2 (GPL-3, `GreenSwampDweller/torchlight-rekindled`, tag `1.0.2`, commit b9973bf)
made its crowd fights faster with three OGRE patches (0025 skeleton poses before the frame on
worker threads, 0026 the visibility mask tested before the camera is announced, 0027 whole
subtrees skipped by visibility flags) and, in its own reimplementation of the game, a worker pool
for those poses, a faster `createPath` (an open list in blocks, a node table kept between
searches), per-face boxes in the collision quadtree and reused particle systems. Its patches
0028-0030 make the draw and update order deterministic; 0031-0032 are iOS's.

None of the OGRE patches applies to our backend: it draws with `RenderSystem::_render` and has no
scene graph, render queue or entities, which is where they act. What has an equivalent here is in
the guest, measured on the profiles of section 2 (POCO X7, `fight.txt` / wave 10 of `fight5`, ms
a frame):

| Rekindled's idea | Equivalent in the guest | Measured | Cost and risk | Decision |
| --- | --- | --- | --- | --- |
| Skeleton poses on worker threads (0025, its job pool) | `cacheBoneMatrices` 0x821B93B0, under `Entity::_updateRenderQueue` 0x821E0D18 in the culling | `Entity::_updateRenderQueue`, main scene: 1.19 / 1.86 (queueing included) | Recompiled guest code on several host threads at once (own contexts and stacks); shared skeleton instances, tag points and the guest's allocator: the hidden-state problem of section 4 plus concurrency | Discarded (the owner, 2026-10-24): too risky |
| Visibility mask before the camera, subtree skip (0026, 0027) | The octree walk 0x821C7158 (Runic inlined `_addToRenderQueue`) in the projector passes | Projector passes' walk 1.43 / 1.89, of which ParticleUniverse's `_updateRenderQueue` 0.38 / 0.32 and the rejected objects' virtuals hundredths | A hook in the walk; skipping `_notifyCurrentCamera` leaves an object's LOD and camera stale | Not worth it: the light map pass draws the particles (a fight capture of 2026-10-18: 22 particle draws, glows and sparkles, into the light map's render target), so at most the shadow pass's share is saved, about 0.2 ms or less |
| A faster `createPath` and collision | `CAstarPathfinder` (constructed at 0x8221F060) and `CPathController` code, 0x8221E000-0x82224000; the collision list (0x82223F90 on) and `CQuadtreeCollision` (0x822E8000 on) | Pathfinding 0.066 / 0.064; collision list 0.26 / 0.10; quadtree 0.05 / 0.15 | Native replacement of a mostly pure function whose path is allocated in the guest's heap | Not worth it: far under 1 ms. The fight scripts do not walk with the stick, so the hero's `setDestination` every frame is not in these profiles; it adds at most one search a frame |
| Reused particle systems | ParticleUniverse system creation in the guest | Weighs on the creation frame, not in the steady fight | High | Not taken |

The determinism patches (0028-0030) do not apply: our draw order is the captured order, and replays
are already byte-identical.

## 8. Ideas from Torchlight II Stutter Fixes (2026-10-29)

`Fancy-Raptor/Torchlight-II-Stutter-Fixes` (MIT per its README; the repository has no licence
file) fixes three Torchlight II stutters in the same engine. Read against our build:

- **The paperdoll's seam weld** (vertex against vertex, replaced by a proximity search): Torchlight
  1 on the 360 has none. The wardrobe build (sub_822DC6B0) and its mesh extractor (sub_823A4078)
  and filler (sub_823A4720) make single per-vertex passes (transform, normals, UVs, bone
  assignments) with no position compare; the PC executable has the same structure. Of the build's
  ~13 ms of meshes and materials, the likeliest super-linear part is the bone assignments: a
  multimap insert per vertex and bone, each node allocated (sub_823A4720), then an equal_range per
  vertex (sub_8245CB50). A possible improvement, not measured (a probe would need the owner's OK).
- **The PAK decompression** (zlib replaced by libdeflate): the game reads `pak.zip` through OGRE's
  ZipArchive, zziplib and zlib 1.2.3 (inflate sub_823E1590), in chunks on a raw-deflate z_stream,
  streaming on demand; a backward seek inflates the entry again from its start. About 0.2 % of the
  main thread in play, but an estimated 10-25 % of a level load's longest frame on the POCO and 30-45
  ms of a 120-160 ms first-use hitch. A level load is about 8.5 s on the POCO, so the gain there is
  2-4 %, about the spread between runs. **Not done (the owner's decision, 2026-10-30)**; the plan
  stays here:
  - The pak (`pak.zip`, Xbox): 20,045 deflated entries, 380 MB (136 MB compressed); p50 2.8 KB, p99
    313 KB, the largest 2.98 MB (`MASSFILE.DAT.ADM`), 12 over 1 MB. Sound is in it (WAV, OGG up to
    532 KB).
  - Hooks on zziplib's read, seek, rewind and close (sub_8258A068, sub_8258A368, sub_8258A258,
    sub_82589B88): on the first read of a deflated entry, its compressed bytes are read through the
    guest's own io (as zzip_file_read does: saveoffset sub_82589C98 on the directory's current fp,
    seek to the data offset, csize bytes through the 32 KB buffer) and the whole entry decoded with
    libdeflate into a host buffer, which serves every read and seek; zziplib's restlen (ZZIP_FILE
    +0xC, usize +0x14, csize +0x18, data offset +0x1C, verified in zzip_file_open sub_82589D10)
    stays the one source of truth (OGRE's tell is usize - restlen). A buffer is freed at
    zzip_file_close, not at the end of the data (FMOD seeks back after reading the end).
  - Memory: resources open, read whole and close one entry at a time (meshes and textures copy the
    stream to memory; skeletons read to the end); FMOD's sounds may stay open while they play.
    Peak under 8 MB typical, about 35 MB worst case (64 open sounds of the largest size). Caps: 16
    MB an entry, 32 MB in all (least recently used evicted and decoded again on demand); over the
    cap, or on any libdeflate result other than the exact size, the guest's own path for that file.
  - Partial reads: none found for meshes, DDS or skeletons; sounds are inflated twice today (FMOD's
    tag probe seeks to the end and back, and zzip_seek inflates again from 0 on a backward seek),
    which whole-entry decoding removes.
  - Verification: all 20,045 entries against the central directory's CRC32 and zlib's output (plus
    truncated and corrupted copies for the fallback); a diag running the guest's path and comparing
    every read and seek; a level-load A B B A on the POCO, which would need a decompression time
    counter to tell 150-300 ms from the run-to-run spread.
- **The clock** (OGRE's Win32 timer pinning the thread to one core on every read): does not apply.
  The guest's OGRE Timer (sub_82423668, sub_824236C8) reads the time base (`mftb`, sub_821F8FE0)
  with no affinity call, which ReXGlue turns into the host's `clock_gettime` or
  `QueryPerformanceCounter` (sdk `src/core/clock.cpp`); the game sets a thread's processor only when
  it creates four of its threads (XSetThreadProcessor).
