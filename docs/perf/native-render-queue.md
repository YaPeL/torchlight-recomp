# Native render queue: plan

Status: plan, approved as a project of its own by the owner (2026-10-22); no code yet. Supersedes
proposals 2 (cheaper per-draw capture) and 3 (the constants path) of the same day, which this one
replaces.

## Why

On the POCO X7 the game's thread is the limit. Walking OGRE's render queue costs 8.2 ms of its
frame in the 30-second fight (`fight.txt`, about 300 draws, simpleperf with our symbols): about
27 us a draw, 17 of them in the guest's embedded OGRE (passes, automatic parameters, constants,
texture units, the per-object work) and 10 in our hooks and capture of each D3D9 call
(`ReadConstants`, the session's state checks, the resource registry, vertex buffer reads). In the
last wave of the 5-minute fight (`fight5.txt`) the same walk is 11.8 ms with about 689 draws: the
cost grows with every object drawn, the dead and the loot included (docs/perf, the fight5
analysis of 2026-10-22).

| Inside the walk (`fight.txt`, ms a frame) | Guest | Ours | Total |
| --- | --- | --- | --- |
| Automatic parameters (`_updateAutoParams` 0x82201EB0) | 0.81 | 0.04 | 0.85 |
| Constants upload (`bindGpuProgramParameters` 0x821C2118) | 0.35 | 0.72 | 1.07 |
| Texture units (`_setTextureUnitSettings` 0x821C93A0) | 0.63 | 0.34 | 0.97 |
| Vertex buffers (`setVertexBufferBinding` 0x821C3E78) | 0.21 | 0.30 | 0.51 |
| Before the draw (`_render` guard 0x821C4058: our draw capture) | 0.49 | 0.87 | 1.36 |
| RTSS per object (`notifyRenderSingleObject` 0x821C4F50) | 0.45 | 0.24 | 0.69 |
| Rest of the pass (`_setPass` 0x821C5730) | 0.73 | 0.30 | 1.03 |
| Rest per object (`renderSingleObject` 0x821C5FA0) | 1.14 | 0.10 | 1.23 |
| Rest of the walk (0x821DD098) | 0.43 | 0 | 0.43 |
| **Total** | **5.25** | **2.91** | **8.18** |

The idea: read the guest's render queue after culling and emit our commands for it directly,
instead of letting the guest's OGRE turn each object into D3D9 calls that our hooks then turn
back into commands. The commands are the same ones the capture produces today
(`commands/types.h`), so the backend, the frontend and the replay do not change, and the two
paths can be compared command by command.

The 2026-10-13 study rated this "F2, native pass renderer" as high risk and XL
(docs/game-thread-parallelism.md). The risk is still there; this plan contains it by going in
stages, each with a per-object fallback to the current path and a verification mode.

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

### The cut point (to confirm in stage 0)

From the disassembly:
- `0x821DD098` (SceneManager vtable slot 26) loops over a render queue group's priority groups. For
  each one it calls `0x821DCFC8` six times, on six collections at `+0x58` and the following
  offsets. That matches OGRE 1.7's `renderBasicQueueGroupObjects`, which calls `renderObjects` on
  a RenderPriorityGroup's six collections: solids basic, solids diffuse/specular, solids decal,
  solids no shadow, transparents unsorted, transparents. Each collection visits
  `SceneMgrQueuedRenderableVisitor` (vtable slots 1 = 0x821CCB88 and 3 = 0x821C0A50), which calls
  `_setPass` 0x821C5730 for each pass and `renderSingleObject` 0x821C5FA0 for each object.
- The cut is therefore **`0x821DCFC8`, one collection at a time**. The native path walks the
  collection itself: OGRE 1.7's `QueuedRenderableCollection` with its pass-grouped map and its
  sorted list. For each pass, it handles the pass itself or hands it back to the guest's visitor
  for that pass and its objects. Falling back by pass keeps the guest's state handling intact for
  anything not understood.
- Stage 0 confirms this mapping (slots, collection layout, organisation mode, which visitor slot is
  which) against the code. Any part that does not match is recorded in `guest_abi` as a Runic
  difference, as the rest of the ABI is.

### What the native path reads from the guest (all through `guest_abi`, big-endian, no casts)

- **The render queue:** RenderQueue → RenderQueueGroup → RenderPriorityGroup → its six
  QueuedRenderableCollection (pass-grouped `std::map<Pass*, std::vector<Renderable*>>`, sorted
  `std::vector<RenderablePass>`). The MSVC `std::map` and `std::vector` layouts are already
  confirmed in `guest_abi/ogre_layout.h`.
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
- **The automatic parameter source:** the camera and its matrices, the current lights, the fog,
  the ambient colour and the time. The guest's `AutoParamDataSource`, as `_setPass` and
  `renderSingleObject` set it up.

### What it emits

The same commands, in the same order, that the capture emits for that pass and those objects:
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
device. The gains are estimates from the profile above, to be measured per stage. Stage 0
measures the share of draws each stage covers. The ranges assume the covered draws lose all of
their guest and capture cost except the native walk's own, which is guessed at 3-5 us per draw.

| Stage | What | Covers (to measure) | Estimated gain, `fight.txt` / `fight5` wave 10 | Risk |
| --- | --- | --- | --- | --- |
| 0 | Confirm the cut and the layouts; count draws per renderable class, pass kind and program; build the comparison harness (section 3) with an empty native path (everything falls back) | - | 0 (the harness's cost, off by default) | Low |
| 1 | Opaque passes, objects without skinning (static level geometry, props, items, StaticGeometry buckets), programs whose automatic constants are all in the supported set, no per-object RTSS listener work | likely most of the level and the loot | 2-4 ms / 3-6 ms | Medium |
| 2 | Skinned entities: bone matrices as `cacheBoneMatrices` left them, the same palette upload | the characters, the dead included | +1-2 ms / +2-3 ms | Medium |
| 3 | Transparent passes in their sorted order, the RTSS per-object listener (lighting, fog, texturing updates) done natively, billboards and particle renderers | effects, decals, UI in the world | +0.5-1 ms / +1 ms | Medium-high |
| 4 | The projector passes (shadow and light textures, their own render targets and cameras) | the 2 projector passes | +1-1.5 ms (their walk) | Medium-high |
| 5 | Culling and the scene graph update natively (the octree walk, `_updateSceneGraph`), feeding the native walk directly | culling, graph | +2-3 ms / +5-6 ms (fight5 wave 10: graph 4.9, octree 5.9 ms) | High |

Stages 1 to 4 together aim at the 8.2 ms of the walk: around 5-6 ms of it back in `fight.txt`,
and 8-9 ms of the 11.8 ms in wave 10 of `fight5`. Stage 5 is a separate decision after them (it is
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
- A stage becomes the default only after fights, the town and a few levels show no difference,
  over a whole session.

**Runtime switch:** `--native_render_queue` (off / on, later per stage). It lives in the same
binary as the guest path, so an A/B turns it on and off without rebuilding, and the build-layout
shift between binaries does not apply (docs/perf/2026-10-22-overnight.md, "Method"). A/Bs follow
the usual rules (A B B A, thermal state recorded, the fight named).

**Replays:**
- An F9 capture records the commands, not the guest's queue, so a replay cannot run the native
  path itself.
- What replays check is the end result: captures taken with the native path on and off, on the
  same frames, replayed byte for byte on GL3+ and GLES (CI's GLES job, Mesa).
- Plus screenshots on the POCO at the same scenes, as for the depth discard.

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
- **Hidden reads of the guest's state by the game:** code that reads the RenderSystem's or the
  AutoParamDataSource's state after the walk, expecting what the guest's walk left there. Stage 0
  looks for readers of those fields after `0x821DD098` returns. Where they exist, the native path
  writes back what the guest would have left.
- **Virtuals of classes not known:** renderables of classes the native path does not know fall
  back, counted by class in the comparison mode, so the coverage is measured, not assumed.
- **Mid-frame changes:** objects or passes changed by a listener during the walk. Listeners on the
  render queue (`fireRenderQueueStarted`/`Ended`, the three virtual calls in 0x821DD098) keep
  running in place, and a pass whose listener does something unknown falls back.
- **Maintenance:** a second implementation of OGRE 1.7's draw path, kept equal by the comparison
  mode, which stays in the diagnostics build for as long as the native path exists.
- **The previous attempt:** the legacy port tried a detached OGRE. This plan does not reuse its
  architecture (CLAUDE.md). Facts from it are checked again in the code before they are used.

## What is needed to start stage 0

- The disassembly of `0x821DCFC8`, the visitor's slots and `_setPass`/`renderSingleObject`, read
  against OgreSceneManager.cpp 1.7.0; `guest_abi` entries for the queue's structures, with
  evidence.
- The comparison harness, with the native path empty (everything falls back, every draw counted
  as "not handled"), measured with the option off to show it costs nothing.
- A draw census per frame, by renderable class, pass kind and program, from a diagnostics build
  on the POCO in `fight.txt` and `fight5.txt`, to size stages 1 to 4 before writing them.
