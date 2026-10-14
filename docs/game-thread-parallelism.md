# Game thread parallelism: feasibility study (2026-10-13)

Read-only study. Question: can the work of the game's thread (`Main XThread`, the recompiled PPC
code, 100 % of one core on every step of the reference route) be spread over other host threads
by reimplementing bounded guest subsystems natively, with the game's thread as the only owner of
the state? Nothing here is implemented. Measured context is in `docs/performance-profile.md` and
`docs/guest-hot-paths.md`.

**Answer in short.** Feasible, but only for a few subsystems and with a modest ceiling. The thread
splits into three blocks: render submission (OGRE's render queue, passes, automatic parameters,
the guest D3D9 layer, the RTSS listeners and our producer: ~45-50 % of the thread), the scene
preparation (culling, skeletal animation, particles: ~25 %), and the game's own logic (~10 % in the
town square, ~18 % in the fight, part of it driving the animation). The first block is serial by
construction and out of scope here. In the second, two subsystems have clear inputs and outputs:
the skeletal animation (bone matrices, ~9 % of the thread in the town square, 7-12 % in the fight)
and the octree culling (~10 %). The game logic has one bounded, pure piece: the A* pathfinder
(~2 % in the town square, more where monsters path). The first candidate is the skeletal
animation, with an expected gain of 5-8 % of the thread (about +5-9 % frame rate when the thread is
the limit); the A* pathfinder is the cheapest proof of the method (1.5-3 %). Everything together
(animation, culling, pathfinding, particles) tops out around 25 % of the thread, so this line of
work gives at best 1.25-1.35x; it does not change the picture by itself. Section 9 looks at the
render submission block itself: native hooks for its hot OGRE functions (8-12 % together), the
work it does that the backend does not use (the device-only skip is done; what is left and why),
and pipelining (not viable for the guest's own emission; a native emission over a snapshot is the
legacy's road, section 9.5).

## 1. Method and its limits

Sources:

- **Profiles.** The town square DWARF profiles of 2026-10-13 (develop, `pc-base-3` and
  `vs-base-cpu3` in the render agent's `out/val-perf`: `perf record -F 499 --call-graph dwarf`,
  10 s each, ~5,000 samples of the game's thread), and the fight profile of 2026-10-07
  (`docs/guest-hot-paths.md`, 18,860 samples, full call chains, folded).
- **Call chains of the 2026-10-13 profiles could not be used.** `perf script` and `perf report`
  return empty call chains for the guest's thread (every frame missing, the leaf included) while
  the host threads of the same file unwind fine; the 2026-10-07 data unwinds no more because its
  binary was rebuilt. Not debugged here (perf 7.0.14, libdw unwinder). The town square numbers
  below are therefore **self time** per function (what `perf report` gives without chains), and
  the attribution of each function to a subsystem is **static**:
  - the RTTI of the XEX (2,024 classes: 1,084 `Ogre::`, 426 `CEGUI::`, 112 `ParticleUniverse::`,
    344 of the game's own `C*` classes, no namespace) gives every vtable and its slots, so 22,615
    slot entries map virtual functions to a class (`tools/guest_re/rtti_vtable.py` logic, run for
    every class);
  - a static call graph of the recompiled code (43,690 functions, 104,983 direct edges) attributes
    the non-virtual functions by the class of their nearest static callers;
  - the functions already identified in `docs/guest-hot-paths.md` keep that identification.
  The class attribution is reliable for virtual functions and for leaves with one caller; it is a
  vote where a helper is shared by several classes. Treat the subsystem shares as +-2 points.
- **Code reading** of the generated code (`generated/default`, the PPC listing in its comments)
  and the OGRE 1.7.0 sources for the functions named below. Hypotheses are marked.
- No game run and no new profile were made for this study.

Note on addresses: the game's hot functions all sit in `0x82190000-0x82220000` whatever their
module (OGRE, CEGUI, ParticleUniverse, the game), which looks like profile-guided hot code
placement by MSVC; an address range tells nothing about the module.

## 2. Where the thread's time goes, by subsystem

### 2.1 The fight (2026-10-07, call chains, inclusive, % of the game's thread)

From `docs/guest-hot-paths.md`, with the new identifications of this study. The producer was
19.5 % of the thread then (8-9 % today), so the guest shares read about 12 % higher today.

| Subtree | % | What it is |
|---|---|---|
| Main loop `0x821F8818` | 90.5 | the rest is thread start, present and GPU waits |
| - Frame rendering `0x821EE0D0` -> `_updateAllRenderTargets` `0x821EE1C0` | 72.4 | |
| -- `SceneManager::_renderScene` of the main camera `0x821B07F0` -> `0x821BC5A0` | 51.0 | |
| --- `renderSingleObject` `0x821C5FA0` | 32.5 | 18.1 guest + 13.4 producer (then) |
| --- `_setPass` `0x821C5730` | 11.0 | |
| -- Culling: `_findVisibleObjects` `0x821A6070` -> octree walk `0x821C7158` | 11.6 | includes `Entity::_updateRenderQueue`, which evaluates the skeletal animation of the visible entities (below) |
| -- Other render targets `0x82198608` (light map, shadows) | 19.9 | |
| - Game logic `0x821F8E20` -> `CGame` vtable slot 0 `0x821F9DA8` | 17.9 | |
| -- `0x821EF318` -> level update `0x821F5CA0` -> `0x821E5910` | 15.2 | the level's per-frame update: units, effects, UI state |
| --- `0x821BB578`, the unit's animation player update (hyp.) | 6.9 | drives OGRE animation states, then `Entity::_updateAnimation` `0x821B9478` (5.95) -> `cacheBoneMatrices` `0x821B93B0` |
| `Entity::cacheBoneMatrices` `0x821B93B0`, all call sites | 6.7 | from the game update above and from `Entity::_updateRenderQueue` `0x821E0D18` (Entity slot 23) during culling |

The game logic's remaining ~8 % (AI, skills, missiles, collision, UI state) is not broken down:
it needs the call chains.

### 2.2 The town square (2026-10-13, self time, two runs, % of the game's thread)

| Subsystem | Run A | Run B | Main functions (self) |
|---|---|---|---|
| Render pass (OGRE 1.7 scene rendering) | 15.1 | 15.5 | `_setPass` 1.4, `renderSingleObject` 0.8 (+0.7 in `0x821C5F18`), `DefaultSceneManager` helpers 2.0, `AutoParamDataSource` 1.9, `_updateAutoParams` (hyp.) 0.6, `StaticGeometry::Region`/buckets 1.2, `Compositor` 0.5, `ManualObject` 0.4 |
| Guest render system (D3D9RenderSystem, buffers, resource map) | 10.3 | 11.5 | `D3D9RenderSystem` state calls 5.1, per-device resource map `0x82581D98` 1.2, `D3D9HardwareBufferManager` 0.7, `GpuProgram` 0.6 |
| Culling | 10.4 | 9.8 | `Frustum::isVisible` `0x821C74A0` 4.0, octree walk `0x821C7158` 3.2, `Frustum`/`Camera` helpers 2.3, `OctreeCamera::getVisibility` 0.7 |
| Game logic (the game's `C*` classes) | 10.2 | 10.5 | A* pathfinder 2.1-2.6 (below), `CGame` helpers 2.2, `CCharacter` update helpers ~2, menu visibility walk `0x821AFE28` 0.4-0.6, `CLayout` 0.4 |
| Producer (host) | 9.6 | 9.1 | `Session::Unchanged` 1.3, `ReadConstants` 1.0, `Session::State` 0.8, `FlatMap::Insert` 1.0, `ResourceRegistry::Lookup` 0.8 |
| Skeletal animation (OGRE) | 8.9 | 9.9 | `Bone` 3.7, `Skeleton` 1.9, `NodeAnimationTrack` 1.4, `AnimationTrack` 0.9, `OptimisedUtil::concatenateAffineMatrices` `0x8219DF38` 0.8 |
| UI (CEGUI 426 classes + the game's menus) | 6.0 | 5.7 | `OgreCEGUIRenderer` 1.0, widget base methods, `Falagard` handlers; the game's menu classes 1.3 |
| Particles (ParticleUniverse) + controllers | 5.3 | 5.4 | `ControllerManager::updateAllControllers` 1.1, its set walk 1.3, `ParticleSystemUpdateValue::setValue` 0.8, `BillboardRenderer` 1.0 |
| libc (host): malloc, free, mem* | 5.1 | 5.3 | behind the guest's `operator new/delete` `0x821CD7F8`/`0x821CD9A8` and the producer |
| RTSS (shader generator listeners) | 3.8 | 3.4 | `SGRenderObjectListener` 1.2, `PerPixelLighting` 0.9, `FFPTexturing`/`FFPFog`/`FFPLighting` 1.5 |
| Host other (SDK runtime 1.1, hooks 1.2, libm `fma` 1.9, std, mutexes) | 9.1 | 8.3 | |
| std trees and maps (guest STL) | 1.8 | 1.8 | `0x8262D6C8` 1.3 |
| Scene graph (`Node::_update`) | 0.6 | 0.7 | |
| Resources, overlays, math, misc | 3.3 | 2.9 | |
| AI (`CAIManager`), collision (`CQuadtreeCollision`), audio | 0.2 | 0.1 | |

Cross-check against `docs/performance-profile.md` (same profiles, resolved by module): guest
code 79 %, producer 8.4 %, libc 4.6 %, `fma` 1.6 %: this table gives guest 77 %, producer 9.1-9.6 %,
libc 5.1-5.3 %, libm 1.8-1.9 %.

### 2.3 The guest's frame

```
main loop 0x821F8818
  CGame update (slot 0, 0x821F9DA8) -> 0x821EF318 -> level update 0x821F5CA0 -> 0x821E5910
      for each unit: slots 63/64/83 of CBaseUnit/CCharacter (0x821A68B8, 0x821D3FD8, 0x821D5410)
          movement, AI, skills; pathfinding 0x8221F100 on demand
          animation player update 0x821BB578 (dt): OGRE AnimationStates, then
              Entity::_updateAnimation 0x821B9478 -> cacheBoneMatrices 0x821B93B0 (synchronous)
  frame rendering 0x821EE0D0 -> _updateAllRenderTargets
      per render target (shadows, light map, window), per camera: _renderScene 0x821B07F0
          first _renderScene of the frame: ControllerManager::updateAllControllers 0x821C8C00
              (ParticleUniverse systems, texture animations)
          _updateSceneGraph (Node::_update)
          culling: _findVisibleObjects 0x821A6070 -> octree walk 0x821C7158
              visible node -> object->_updateRenderQueue
                  Entity 0x821E0D18: cacheBoneMatrices (if not done this frame), queue sub-entities
                  StaticGeometry::Region: queue buckets (bucket culling hook here)
                  ParticleUniverse: fill billboard vertex buffers
          _renderVisibleObjects: _setPass 0x821C5730, renderSingleObject 0x821C5FA0,
              _updateAutoParams, RTSS listeners, D3D9RenderSystem calls -> our hooks -> producer
  present (VdSwap)
```

Two facts matter for parallelism. The skeletal animation is evaluated **lazily and twice over**:
once by the game's animation players during the game update (synchronous, the game then reads the
bones: attachments, effects) and once per frame by `Entity::_updateRenderQueue` for the visible
entities the game did not touch (`Entity::cacheBoneMatrices` guards both with the frame number,
OgreEntity.cpp:1157). And the particle update happens at one point of the frame (the first
`_renderScene`), isolated in time from the rest.

### 2.4 Call chains with frame pointers (2026-10-14)

The DWARF unwinding failure of section 1 was worked around with a profiling build that keeps frame
pointers in every target, the recompiled game included (`TORCHLIGHT_FRAME_POINTERS`, off in the
presets and releases; `-fno-omit-frame-pointer` reaches the generated code because the option is
set before the SDK's `rexglue.cmake`), and `perf record -F 499 --all-user --call-graph fp` on the
scripted route `measure-fight-town` (fixed-floor save, no cap, no vsync, 720p): 25 s from the
"DUNGEON: fight" mark (12,422 samples of the game's thread) and 12 s from "TOWN SQUARE" (5,980).
The scripted fight is light (the character walks west attacking whatever is near; it never reaches
a real fight), so the fight column is indicative; the town square is the reference route's. Each
sample is attributed to the nearest marked frame from the leaf up (the subtree roots of section 2.3,
the vtable classes for the rest), so a `Bone` function under the animation player counts as
animation and a `malloc` under the producer as producer.

| Subsystem (nearest marker from the leaf) | Fight | Town square |
|---|---|---|
| Producer (host) | 14.1 | 12.0 |
| Render pass, other (`_renderVisibleObjects`, scene manager helpers, static geometry) | 9.6 | 9.1 |
| Game logic (the game's classes), of which the level update glue | 8.5 (2.3) | 7.6 (2.0) |
| Skeletal animation (`Bone`, `Skeleton`, tracks), of which the animation player glue | 7.5 (1.2) | 10.0 (0.8) |
| Culling (octree walk, frustum, camera) | 6.3 | 10.5 |
| Guest render system (`D3D9RenderSystem` state calls, buffers, lookups) | 5.5 | 4.9 |
| Particles (ParticleUniverse) + controllers | 5.3 + 4.1 | 3.3 + 2.6 |
| RTSS per-object listener | 4.9 | 4.6 |
| UI (CEGUI + the game's menus) | 4.4 + 1.5 | 4.3 + 1.3 |
| Texture units (`_setTextureUnitSettings`, `_setTextureBlendMode`) | 2.4 | 2.6 |
| `renderSingleObject` / render queue / `_render` / binds | 2.4 / 2.3 / 2.1 / 2.4 | 2.3 / 2.5 / 2.1 / 2.3 |
| Automatic parameters / bind params | 2.1 / 1.5 | 2.1 / 1.5 |
| Scene graph / `_setPass` / pathfinding (A*) | 1.3 / 1.1 / 0.9 | 2.0 / 1.2 / 2.3 |
| SDK runtime / our hooks | 1.8 / 1.6 | 1.8 / 1.7 |

Inclusive, by subtree (fight / town): frame rendering 74.7 / 72.9; the visible objects' passes
(`0x821DD098`) 40.5 / 38.5, `renderSingleObject` 29.3 / 26.0, `_setPass` 9.3 / 10.4, the
per-pass parameter update (slot 41, with the bind and the producer's `ReadConstants` inside)
10.3 / 9.6, `_render` with its hook and the draw capture 10.2 / 8.3, `_setTextureUnitSettings`
with its setters and hooks 6.5 / 6.8, the RTSS listener 5.1 / 5.0; culling (`_findVisibleObjects`)
15.1 / 18.6; the other render targets 22.8 / 24.6; game logic 23.2 / 25.1, of which the level
update 19.6 / 20.1.

Two facts the self-time attribution could not show:

- **The skeletal animation is evaluated almost entirely from the game update.** The animation
  player update `0x821BB578` is 8.7 / 10.8 % inclusive and `Entity::_updateAnimation` under it
  7.5 / 10.0, while `cacheBoneMatrices` from every call site is 7.5 / 9.9: the render-time path
  (`Entity::_updateRenderQueue`) finds the bones already computed. Candidate A's parallel tier
  (section 3) therefore has nothing to overlap; what remains is the native speedup of a
  synchronous evaluation, 2-4 % of the thread, not 5-8 %.
- **A layout update of 7.3 % in the fight** that the town square does not show: `0x82198878`
  -> `CLayout` slot 64 `0x821B7650` -> `CEditorScene`/`CLayoutScene` slot 64 `0x821B7588` (self
  1.4 %) -> `CLayoutDescriptor` slot 7, under the level update: a per-frame walk over the level's
  layout scenes and their objects. Game logic, Runic's; a candidate to understand before anything
  else in that block (section 10).

The hook wrappers themselves (the strong `sub_X` symbols as leaves) are 1.6-1.8 % of the thread:
the recording bodies, inlined.

## 3. Candidates

Sizes: S/M/L/XL. "Share" is of the game's thread in the town square unless noted.

### A. Skeletal animation (bone matrices)

- **Share.** ~9-10 % self (section 2.2) plus its part of libm `fma` and the allocator: 9-11 %. In
  the fight the game-update path alone was 6.9 % inclusive (2026-10-07), the render-time path on
  top of that.
- **Reads.** Per entity: its `AnimationStateSet` (time position, weight, enabled, blend mask per
  state), the `SkeletonInstance` (bone hierarchy, binding pose, inherit orientation/scale flags,
  manual bones, blend mode), the `Animation`s and their `NodeAnimationTrack` key frames
  (immutable after load), the parent node's full transform (for `mBoneWorldMatrices`).
- **Writes.** `mBoneMatrices` (one 4x4 per bone), `mBoneWorldMatrices` (hardware skinning
  constants, read by the automatic parameters), the bones' derived transforms (read by `TagPoint`s:
  weapons and attached effects), `mFrameBonesLastUpdated`.
- **Isolation.** Good. Pure math over per-entity data, and the entities are independent of each
  other. OGRE 1.7.0 has the whole reference implementation (`Skeleton::setAnimationState`,
  `Animation::apply`, `NodeAnimationTrack::applyToNode`, `Node::_update`,
  `Skeleton::_getBoneMatrices`, `Entity::cacheBoneMatrices`); Runic patches to it are the
  unknown, found by the verification mode.
- **Identical result?** Visual, and the attachment positions the game reads (missile origins,
  weapon trails) are visual too as far as is known; combat uses `CDamageShape`s and stats, not
  bones (hypothesis). Bit-identical matrices are achievable where the guest's arithmetic is
  reproduced (the recompiled `fmadd` is already libm's `fma` on the host; VMX `vmaddfp` likewise),
  and the verification mode measures the difference rather than assuming it.
- **Difficulty.** M-L. The readers of the guest structures (`guest_abi/`), the port of the
  evaluation (interpolation modes: linear and spline, rotation modes, blend modes, weights,
  inherit flags, manual bones), the write-back in big-endian layout, and the batch scheduler.
- **Parallel scheme.** Two tiers. (1) The render-time evaluations: after the game update and
  before `_updateAllRenderTargets`, the entities whose animation state changed this frame and were
  not evaluated by the game are known (hook on the state's dirty frame); evaluate them on a worker
  pool while the main thread walks the octree; the hooked `cacheBoneMatrices` consumes a ready
  result or computes natively when it is not. (2) The game-update evaluations (the animation player
  forcing `Entity::_updateAnimation`) stay synchronous: there only the native speedup counts. The
  world-space concatenation (`concatenateAffineMatrices`) stays at consumption, because
  `_updateSceneGraph` runs inside `_renderScene`, after the batch.
- **Expected gain.** 5-8 % of the thread: the render-time tier off the thread entirely, the
  game-update tier 2-3x faster natively (contiguous data, no context stores).

### B. A* pathfinder

- **Share.** 2.1 % (run A) to 2.6 % (run B) in the town square (NPCs walking); unknown in the
  fight, where monsters path (the call chains are needed; expect more).
- **What it is (confirmed by reading).** `0x8221F100` finds a path; `0x8221F8B8` -> `0x8221FB00`
  expands the eight neighbours of a node with corner-cutting checks through `0x8221F7C8`
  (walkable(x, y), 26 instructions, no calls); `0x8221FEB0` (the hottest, 0.7-1.0 % of samples)
  creates or updates a node: `g` = parent `g` + a step cost from a table, `h` = dx^2 + dy^2 (no
  square root), `f = g + h`, a 64-byte node allocated with the guest's `operator new`, inserted in
  an **open list kept as a sorted linked list** (O(n) insertion) and a closed list, 8 neighbour
  pointers per node. Callers: `0x8228F8C8` and `0x8228FFE8`, reached from `CMonster` slot 106 and
  `CCharacter` slot 108 (unit movement).
- **Reads.** The level's walkability grid (static per level; whether dynamic blockers like
  breakables and doors write it is to be confirmed), start, goal, limits. **Writes.** The path
  (node list) returned to the unit.
- **Isolation.** Excellent: a pure function of grid + start + goal.
- **Identical result?** Required: the path moves monsters, which is gameplay. Achievable by a
  faithful port (same neighbour order, same costs, same tie-breaking on equal `f`, same iteration
  limit); the sorted-list insertion order on ties must be reproduced by the heap (stable key).
- **Difficulty.** S-M: about eight functions, ~1,000 PPC instructions, no source; the grid's
  layout and the tie-breaking are the work.
- **Parallel scheme.** None without changing behaviour: the unit expects the path in the same
  call. An asynchronous request with a one-frame delay would change movement, so the gain is the
  native speedup only (binary heap instead of the sorted list, contiguous closed set instead of
  64-byte heap nodes: 5-10x on the hot path).
- **Expected gain.** 1.5-2.5 % in the town square; 2-4 % where monsters path (to measure).

### C. Culling (octree walk and frustum tests)

- **Share.** ~10 % (walk 3.2, `Frustum::isVisible` 4.0, camera and `getVisibility` 3.0); the
  `_updateRenderQueue` calls on visible objects are inside the 11.6 % of the fight but are not
  culling work.
- **Reads.** The octree (octants, loose bounds, node boxes), each node's world box
  (`OctreeNode::_updateBounds`), the camera's frustum planes, object flags and masks.
  **Writes.** The sequence of `_updateRenderQueue` calls (the render queue), in the walk's order.
- **Isolation.** Medium. The walk is pure; a native mirror of the octree needs hooks where nodes
  are inserted, removed and re-bounded (Runic's iterative walk `0x821C7158`, `_updateBounds`
  `0x821BE630`, `Octree::_addNode`/`_removeNode`), and the output is a list of objects to queue
  in the walk's exact order, consumed by the guest thread which still calls their
  `_updateRenderQueue`. Three cameras per frame (shadows, light map, window) can be walked in
  parallel with each other and with the main thread's `_updateSceneGraph`.
- **Identical result?** Required: the same objects queued in the same order (render order and the
  pass sorting's ties). The possible visibility-flag bug noted in `docs/guest-hot-paths.md` must
  be reproduced or deliberately fixed, and the bucket culling hook kept.
- **Difficulty.** M-L (the mirror and its invalidation; the SoA frustum test itself is small).
- **Expected gain.** ~6 % (the walk and tests become 5-10x faster in SoA form and can overlap;
  the queueing stays).

### D. Particles (ParticleUniverse) and the controller walk

- **Share.** ~5 % (controller walk and set 2.4, particle updates 0.8, billboard fill 1.0, plus
  the texture controllers).
- **Isolation.** Poor to medium. 112 classes, scripted systems (emitters, affectors, observers,
  event handlers), systems touch scene nodes, the camera and materials, and the billboard
  renderer fills vertex buffers through our lock/unlock hooks. The update is isolated in time
  (one point of the frame), and each system is independent of the others, which makes it
  data-parallel in principle; but running the guest's own code for each system on worker threads
  races on OGRE's scene graph (`needUpdate` propagation), the pools and the guest allocator, and
  a native reimplementation of ParticleUniverse is XL.
- **Identical result?** Visual only, as far as is known (observers trigger particle events, not
  game events; to confirm for `CParticleWrapper` observers).
- **Verdict.** Not a first candidate. The 2026-10-09 experiment (the controller walk moved to the
  host, no gain) shows the walk's cost is memory latency over the guest's heap, which only a
  native data layout removes.

### E. UI (CEGUI and the game's menus)

- **Share.** ~6 %. Window hierarchy walks, widget updates, the CEGUI renderer's quads, the game's
  menu visibility walk `0x821AFE28` (30 instructions, 0.4-0.6 % of the thread: called very often).
- **Isolation.** Poor: the menus read game state everywhere and CEGUI is 426 classes.
  **Verdict.** Not a candidate for native reimplementation. The hot visibility walk is a small
  item (section 7).

### F. Render submission (render pass, D3D9 layer, RTSS, producer)

- **Share.** 38-40 % of the thread (15 + 10.5 + 3.6 + 9.3), the largest block.
- **Isolation.** It is the serial consumer of the whole scene state (materials, passes, automatic
  parameters, light lists, the RTSS per-object listeners) and it already feeds another thread (the
  backend). Taking it off the guest thread means a native OGRE 1.7 pass renderer reading guest
  materials, which `docs/guest-hot-paths.md` rates high risk and the legacy attempted. The
  remaining measured option is phase B of the producer (its work off the thread, 1.5-2 %,
  `docs/performance-profile.md`).
- **Verdict.** Out of scope here; named because it caps everything else (section 6).

### G. Small ones

Scene graph update ~0.7 %, AI manager and collision quadtree ~0.2 %, resources ~1 %: below the
noise of this machine, not candidates.

## 4. Ranking

| # | Candidate | Expected gain (thread) | Risk | Size | Why this place |
|---|---|---|---|---|---|
| 1 | A. Skeletal animation | 5-8 % | M | M-L | Largest clean subsystem; reference source exists; two independent entity sets, one of them parallel |
| 2 | B. A* pathfinder | 1.5-2.5 % town, 2-4 % fights | L | S-M | Pure function, exact comparison, cheapest proof of the method and of the verification mode |
| 3 | C. Culling | ~6 % | M-H | M-L | Needs an octree mirror and order-exact output; the bucket culling already took the visible part |
| 4 | F1. Producer phase B | 1.5-2 % | L | M | Already planned; waits for a machine without a thermal limit |
| 5 | D. Particles | <= 5 % | H | XL | No reference source, racy data, visual only |
| - | E. UI, F2. native pass renderer | | H | XL | Not recommended |

## 5. First candidate and proof of concept: skeletal animation

Staged so each stage has a done criterion and can stop.

1. **Measure with call chains (needs a new profile; ask the render agent before running).**
   Fix or work around the empty chains of `perf script` on the guest's thread (try the current
   binary's build-id in perf's cache, `--call-graph lbr` as the fallback known to work with the
   32-branch limit). Report `Skeleton::setAnimationState`, `_getBoneMatrices`,
   `Entity::_updateAnimation` and `cacheBoneMatrices` inclusive, split by caller (game update vs
   `_updateRenderQueue`), in the town square and the fight. Done: both shares known; go on only if
   the total is 7 % or more.
2. **Layouts.** `guest_abi/` entries for `Entity` (`mAnimationState`, `mSkeletonInstance`,
   `mBoneMatrices`, `mBoneWorldMatrices`, `mFrameBonesLastUpdated`, `mSkipAnimStateUpdates`;
   the code at `0x821B93B0` already fixes `+208`, `+384`, `+396`, `+440`, `+444`, and
   `Root::mNextFrame` at `+224`), `AnimationStateSet`, `AnimationState`, `Skeleton`, `Bone`,
   `Animation`, `NodeAnimationTrack`, `TransformKeyFrame`, each confirmed against accesses in the
   recompiled code, Runic differences marked. Done: the readers pass unit tests on captured guest
   memory of a town square frame.
3. **Native evaluation, synchronous, in shadow.** A hook on `Entity::cacheBoneMatrices` that lets
   the guest run as today and, with a cvar on, also evaluates natively and compares the bone
   matrices (bit-exact count, maximum absolute and relative difference, first mismatch per
   skeleton). No behaviour change. Done: the reference route by hand with zero mismatches above a
   stated tolerance, or the Runic differences found and ported.
4. **Native authoritative, synchronous.** The hook writes the native result and skips the guest
   evaluation (the guest stays as the fallback and as the shadow in the verification mode).
   Measured on the reference route. Done: the frame rate and the thread profile show the speedup
   on the game-update tier; replays identical (section 6).
5. **Parallel tier.** A hook at the end of the game update (or at `_updateAllRenderTargets`
   entry, `0x821EE1C0`) enqueues the dirty, not yet evaluated entities to a worker pool;
   `cacheBoneMatrices` consumes or waits. The workers read guest memory only and write to host
   buffers; the write-back to guest memory happens on the guest thread at consumption. Done: the
   same validation as stage 4, plus the pool's wait time per frame in the periodic summary
   (should be about zero).

Stage 3 is the point where the plan is either confirmed or stopped: if Runic's animation code
differs from OGRE 1.7 beyond what the mismatches explain, stop and report.

The A* pathfinder (candidate B) is the alternative proof of concept if a smaller first step is
preferred: stages 2-4 apply unchanged (readers for the grid and the node; a shadow mode comparing
the returned path node by node; then authoritative), with the exact comparison that animation
cannot promise.

## 6. How identical results would be verified

- **Shadow mode** (the mechanism of stage 3): the native function runs next to the guest's,
  compares outputs in guest memory, logs counts and first differences per site. It is the same
  idea as the SDK patch 32's `verify_non_argument_locals` check, and it is the only method that
  verifies the subsystem on its own, with the game authoritative. Exact for A* (paths), tolerance
  with statistics for matrices.
- **Session replays.** `--live_record` and `replay --session` compare the frames the backend
  consumed: with the native result authoritative, the frames of the same route must be identical
  where the subsystem has no visual effect (A*), or differ within the tolerance's visual noise
  (animation). The bucket culling validation (`docs/guest-hot-paths.md`) is the model: sampled
  frames, several aspect ratios, every difference explained.
- **Saved games.** The fixed-floor save loaded and saved again, compared against develop's own
  run-to-run differences (AI state, positions, timers), as done for the codegen options. The game
  is not deterministic run to run (monster randomness; `docs/performance-profile.md` on the hand
  route), so this detects gross divergence only.
- **Automated routes.** `--dev_input_script` routes for the dungeon and the town, with the
  shadow mode on, as the regression test of each stage; the 60 unit tests for the readers.
- **What cannot be verified this way.** Timing-dependent behaviour: a native speedup changes
  frame times, and the game's logic uses `dt`, so monster paths and positions diverge from a
  guest-only run for that reason alone. Comparisons are therefore per call (shadow mode), not
  per session.

## 7. Risks, and what would make the plan unviable

- **The ceiling.** With animation, culling, A* and the producer's phase B all done, about 20-25 %
  of the thread moves or shrinks: 1.25-1.35x. The render submission (~40 %) stays on the thread.
  If the goal is a large step, this is not the line; the render submission is.
- **Attribution error.** The town square shares come from self time and a static attribution
  (section 1). If the call chains show the animation's inclusive share under 5 %, candidate A
  drops below the noise this laptop can measure and the ranking changes (stage 1 is the gate).
- **Synchronous consumption.** The game update evaluates bones for the units it animates. In the
  fight the animation player path was 6.9 % inclusive while `cacheBoneMatrices` from all call
  sites was 6.7 %, so most evaluations there may be the synchronous ones. If that holds in
  general, candidate A gives its speedup only (2-4 %), not the overlap.
- **Runic differences.** Torchlight's OGRE is 1.7.0 "possibly with Runic's patches"; the
  animation code is a likely place for them (the game's own `CAnimationPlayer`,
  `CActiveAnimation`, `CTimeline` sit on top). Found at stage 3; if large, the port grows to L.
- **Data races.** Worker threads may only read guest memory in a window where the main thread
  does not write the inputs (animation states are written by the game update; node transforms by
  `_updateSceneGraph` inside `_renderScene`), and must never allocate on the guest heap or call
  guest code. Results go to host buffers and are written back on the guest thread.
- **Floating point.** The guest's `fmadd`/`vmaddfp` are fused; the native code must use `fma`
  where the guest did, in the same order, or accept and bound the difference. Denormal handling
  (VMX flushes) differs from SSE defaults.
- **Big-endian write-back.** Every matrix written to guest memory goes through the `guest_abi/`
  helpers; a host struct cast is a silent corruption.
- **Order and ties (culling).** The render queue's order must be the guest's; a faster walk that
  queues in another order changes the sorting's ties and the picture.
- **Tooling.** The DWARF call chains of the guest's thread are currently unusable in `perf
  script` on this machine; until fixed, inclusive shares cannot be measured, which blocks stage 1
  of any candidate.

## 8. Small items noted, not done

- **A\*'s open list** is a sorted linked list with O(n) insertion and 64-byte heap nodes
  (`0x8221FEB0`, `operator new` per node): its cost is algorithmic, not the recompiled code's.
  Part of candidate B; nothing to do in the guest code.
- **The menu visibility walk** `0x821AFE28` (recursive, 30 instructions) is 0.4-0.6 % of the
  thread by itself, i.e. called tens of thousands of times a second; who calls it that often is
  worth one look with the call chains (a per-unit or per-widget check that could be cached).
- **Guest `operator new`/`delete`** are 0.3 % themselves and bring libc's 5 %: the per-frame
  allocators are the A* nodes, CEGUI strings and OGRE's per-frame containers (light lists,
  render queue groups). Not a subsystem; noted for the allocator discussion.
- **Entity::_updateAnimation `0x821B9478` is called from one place only** (the animation player
  update `0x821BB578`), and `Entity::_updateRenderQueue` `0x821E0D18` calls `cacheBoneMatrices`
  directly (the 1.7 `updateAnimation` inlined). The hook point for candidate A is therefore
  `cacheBoneMatrices` `0x821B93B0`, which both paths share.

## 9. The render submission block: three ways to shrink it (2026-10-13)

The block is everything between the render queue and our producer's commands: OGRE 1.7's pass
rendering (`_setPass`, `renderSingleObject`, the automatic parameters, the RTSS per-object
listeners), the guest D3D9 render system under our hooks, and the producer itself. In the town
square (self time, % of the game's thread, the two runs of section 2.2): render pass 15.1-15.5,
guest render system 10.3-11.5, RTSS 3.4-3.8, producer 9.1-9.6, hooks 1.2, plus about 3-4 points of
libc and std behind them: **42-45 % of the thread**. In the fight (2026-10-07, inclusive) the main
scene's `renderSingleObject` and `_setPass` were 43.5 % with the producer of that time inside, and
the two render textures' passes more inside their 19.9 %. It is serial by construction: one
RenderSystem, one state, one order.

Identified for this section (vtable slots from `guest_abi/ogre_layout.h` and the RTTI map; the
rest from the code): `0x821C26D0` is `SceneManager` slot 41, the per-pass GPU parameter update
(calls `0x82201EB0` three times, `GpuProgramParameters::_updateAutoParams`: 2,284 instructions,
258 indirect calls, writing through `0x821C28D8`), 9.6 % inclusive in the fight with
`bindGpuProgramParameters` `0x821C2118` (slot 91) and the producer's `ReadConstants` inside;
`0x821C4EE0` is `SceneManager` slot 13, the `renderSingleObject` listener dispatch, 4.5 %
inclusive, all of it the RTSS `SGRenderObjectListener::notifyRenderSingleObject` `0x821C4F50` and
the sub render states' parameter updates; `0x821C93A0` is `_setTextureUnitSettings` (slot 32),
7.5 % inclusive with its per-unit setters and our hooks; `0x821C3730` is `RenderQueue::addRenderable`
(the `BaseWhite` fallback); `0x821C52B0` is `_setSceneBlending` (slot 51, class A, skipped since).

### 9.1 Path 1: faster on the thread, native hooks for the hot OGRE functions

The model is the culling and copy hooks: a pure or well-bounded guest function replaced by a host
implementation that reads the same guest objects through `guest_abi/`, writes the same members,
and keeps the guest's code as the fallback behind a cvar. What it buys is the recompiled code's
overhead (context stores, indirect dispatch, byte swaps, the guest allocator) on code that is
mostly glue; what it cannot buy is the virtual calls into guest objects that stay guest code.

| Function (guest) | Share (town, self) | Reads | Writes | Clear I/O? | Size | Risk | Gain |
|---|---|---|---|---|---|---|---|
| `_setTextureUnitSettings` `0x821C93A0` with `_setTextureBlendMode` `0x821C9100` (members only, step a) and the per-unit setters our hooks record | ~2 % (7.5 % incl. in the fight, hooks inside) | `TextureUnitState`: texture, filtering, addressing, anisotropy, mip bias, blend modes, coord set, texgen, texture matrix | `mTexStageDesc[]`, the blend block at `+144 + 32 * unit`, and the per-unit RenderSystem calls (our hooks' records) | Yes: the class B evidence of `docs/native-skip-guest-d3d.md` already lists the members and their readers | M | M (every member the guest reads back must be written as the guest did) | 1.5-2.5 % |
| Class B binds: `setVertexDeclaration` 84, `setVertexBufferBinding` 85 `0x821C3E78`, `_setTexture` 39 `0x821C8E40`, `_render` 87 `0x821C4058` up to the skipped device call | ~2.5 %: the per-device resource map `0x82581D98` 1.2, the tree lookup `0x8262D6C8` 1.3 (shared with others), their own bodies 0.5 | the buffer or texture object, its per-device resource map (hypothesis: `D3D9HardwareBuffer::getD3D9VertexBuffer(device)`, a `std::map` by device), the system memory shadow and its flush flag | `mLastVertexSourceCount`, `mTexStageDesc[].pTex/texType`, the base `_render` counters and clip planes; the device resource found is only used by the skipped call | Yes: the skip left these functions running up to the device call; what remains is the lookup of a resource nobody uses, plus the members | S-M | L-M | 1.5-2.5 % |
| RTSS per-object listener `0x821C4EE0` -> `0x821C4F50` -> `PerPixelLighting`, `FFPTexturing`, `FFPFog`, `FFPLighting` `updateGpuProgramsParams` | 3.4-3.8 % (4.5 % incl. in the fight) | the pass's generated render state (per pass, static after generation), the renderable's light list, the units' texture matrices, the scene's fog | GPU parameter constants of the pass (light position and direction in view space, colours, texture matrices, fog parameters) in the guest's parameter buffer | Yes, per sub render state; Runic's RTSS is partly documented (`src/rtss/`, its skinning) | M-L | M | 2.5-3.5 % |
| `_updateAutoParams` `0x82201EB0` with slot 41 `0x821C26D0` and `_writeRawConstants` `0x821C28D8`, plus the `AutoParamDataSource` getters (`0x821CEF88`, `0x824639B8`, `0x821BEF28`) | ~3 % (9.6 % incl. in the fight with the bind and the producer inside) | the program's auto constant list (static per program; the captures say which types the game uses), the data source: camera matrices (`Frustum` virtuals), the renderable's world matrices (`getWorldTransforms`, a guest virtual per renderable), its light list, viewport, time, fog, pass colours | the float constant buffer of the `GpuProgramParameters`, which the bind hands to our hook | Medium: the outputs are plain floats, the inputs come partly through guest virtuals that stay guest code | L | M (the matrix order and `fma` placement decide bit identity; nothing in the guest reads the result back except the bind) | 2-3 % |
| `_setPass` `0x821C5730` | 1.4-1.6 % (11 % incl. in the fight) | the `Pass`, the `SceneManager`'s shadow and fixed-function state | ~20 RenderSystem calls (hooked), `mAutoParamDataSource`'s current pass, scene manager members | Partly: many branches (shadow techniques, program binding, pass iteration, fixed-function fallbacks) | L | H | <= 1.5 % beyond the rows above |
| `renderSingleObject` `0x821C5FA0` and `0x821C5F18` | ~1.5 % | every renderable's virtuals: `getWorldTransforms`, `getNumWorldTransforms`, `getLights`, `getRenderOperation`, `preRender`/`postRender`, custom parameters | the rows above, then `_render` | No: a dispatcher over guest virtuals; native code would still call them through the recompiled code | XL | H | not recommended (`docs/guest-hot-paths.md`, #5) |
| Render queue traversal: `acceptVisitor` `0x821C08B8`, the visitor slots `0x821C0A50`/`0x821CCB88`/`0x821D27F8`, `addRenderable` `0x821C3730`, the sort | ~1 % | the queue's groups, priorities and pass maps | the calls to `renderSingleObject` in order | Yes | S-M | L | < 1 %: not alone |

Together the first four rows are 8-12 % of the thread, comparable to the skeletal animation
(candidate A), with one advantage: every one of them is verified by the live session recordings
(`--live_record` + `replay --session`), because their only observable output is the command
stream, which must come out identical. The order would be the class B binds (smallest, the lookup
is pure waste), then `_setTextureUnitSettings`, then the RTSS listener and the automatic parameters,
which share the parameter-buffer writer.

### 9.2 Path 2: less work, what the guest produces that the backend does not use

**Done.** The device-only calls (class A: sampler and texture stage states, render states, the
device part of draws and bindings, the texture matrix once slots 41 and 42 keep their members,
the vertex declaration): +8-11 % game thread, on by default since 2026-10-11
(`docs/native-skip-guest-d3d.md`). The guest's memory copies run as host `memcpy` (+2-5 %).

**Tried and discarded (2026-10-11).** Step c, the device `SetTexture` inside slot 39, and step d,
the shader and constant uploads inside slots 90, 91 and 93 (branches `feature/skip-settexture`,
`feature/skip-programs`). The commands stayed identical and the skipped functions left the
profile, but their weight was about 0.25 % of the thread (constant uploads 0.15, `SetTexture`
0.10), and the hand runs showed a 2-3 % *lower* presented rate with them, cause not found (the
scripted runs had forked routes, so they could not decide). The user's decision: not in. The
lesson for this section: the remaining device calls are cheap; class B's remaining cost is the
members and the resource lookups (path 1, row 2), not the device.

**Kept on purpose (class C).** Frame begin and end, render targets, clears and resolves, the swap
and the fences: the guest waits on them (`hooks/gpu_wait_hooks.cpp`); nothing to skip.

**Still produced by the guest and recomputed by us: the shader parameters.** The backend does not
run the guest's HLSL programs: it reads the guest's constant block through the structure of the
RTSS program (`src/rtss/`, `ProgramInputs` in `backend/backend.cpp`: world and clip matrices,
lighting on or off, material colours, one light's view-space direction and colour, fog, the skin
palette, the alpha test) and generates its own OGRE 14 RTSS program per state structure, then runs
OGRE 14's `_updateAutoParams` on the host. So the guest's `_updateAutoParams` and RTSS listeners
(6-7 % of the thread) compute about twenty values per draw that we only use as inputs. The
alternative is to hand the backend the sources instead: world matrices (already commands from
slots 28, 30, 31: `SetMatrix`, `SetWorldMatrices`), the renderable's lights (positions, colours,
from the `Light` objects), the pass's colours, the scene's fog, the units' texture matrices, the
bone palette (candidate A's output), and to skip the guest's parameter computation for those
programs. That is a new command set and a new backend path next to the present one. Size L, risk H
(every lighting, fog and material convention of the guest's RTSS reproduced from sources instead
of read from values; the captures and replays would change format), gain up to 6 %. Path 1's
native versions of the same two functions give 4-6 % of that at lower risk and leave the
interface alone; path 2's version only makes sense if path 1's turns out insufficient.

**The two render textures (shadows, light map).** About 20 % of the thread in the fight is the
guest drawing two 512x512 textures per frame through the same submission path (their walks are
cheap: 0.3-0.35 ms each in the town square, `docs/guest-hot-paths.md`). Their content is sampled
by the main pass, so the draws cannot be dropped. "Less" here would be reuse: skip the guest's
`_renderScene` of a render texture when nothing it draws changed since the last frame (its camera,
the lights, the casters' transforms, their animations) and keep the backend's texture. Generic in
principle (a change counter over the walk's inputs), but the light map likely changes every frame
(animated lights, particles) and the shadows every frame a caster animates: before anything, a
replay should measure how often two consecutive frames of each texture are identical. Size M,
risk M-H, gain unknown (0 to 10 %). Speculative; listed, not proposed.

**Draws entirely off screen.** After the bucket culling, the remaining fully off-screen world draws
are the non-bucket objects queued because their octree node's box touched the frustum: 1-14 % of
the former off-screen set (`docs/guest-hot-paths.md`: the buckets were 86-99 % of it). A
per-object frustum test in the walk hook (the object's world box against the six planes, the
camera's culling frustum when set) drops them before `_updateRenderQueue`, which also spares their
passes, parameters and our producer. Identical pictures by construction, validated like the
bucket culling. Size S, risk L, gain 1-2 % of the thread. The small, obvious item of this section.

**The producer.** Phase B (its work off the thread, 1.5-2 %) and the two phase A leftovers
(0.5 %) are measured and wait for a machine without a thermal limit
(`docs/performance-profile.md`).

### 9.3 Path 3: pipelining, the game simulates frame N+1 while another thread emits frame N

What the emission reads while it runs (`_renderVisibleObjects` and below): the render queue
(pointers to live `Renderable`s and `Pass`es), each renderable's world transforms (scene node
derived transforms, bone world matrices for skinned entities), its light list (computed lazily at
render time from the lights' current positions), the materials, techniques, passes and texture
unit states (shared objects that change at runtime: the texture animation controllers write frame
and transform every frame, Runic's effects change pass colours and parameters), the
`GpuProgramParameters` buffers (written by the automatic parameters during the emission), the
`AutoParamDataSource` (camera, viewport, lights, fog, time), the `RenderSystem` object's state
(`mTexStageDesc`, matrices, counters), the `SceneManager`'s state, and the hardware buffers'
contents: particles, CEGUI and overlays rewrite their vertex buffers inside the same frame's
`_updateRenderQueue` and lock/unlock paths.

To emit frame N while the game thread computes N+1, all of it has to be frozen for N. There are two
ways, and neither is viable with the guest's own emission:

- **Run the guest's `_renderVisibleObjects` on a second guest thread.** Mechanically possible: the
  guest memory is one shared mapping and the SDK runs several XThreads already (the game has
  seven). But the game thread mutates every input above while it simulates N+1 (positions,
  animation states, materials, buffer contents, the allocator), the guest OGRE is a single-threaded
  build (no locks on its containers; hypothesis: `OGRE_THREAD_SUPPORT 0`, the 1.7 default), and the
  D3D runtime's device state is one object. Every object the emitter touches would be a data race
  with the simulation, with no way to add copy-on-write to Runic's code. The only safe variant
  blocks the game thread during the emission, which is no overlap.
- **Snapshot the render-relevant state at the end of queueing, then emit from the copy.** A copy of
  every queued (pass, renderable) with its transforms, lights, pass state, texture units, parameter
  buffers and buffer contents is a frame description, i.e. a command stream one level above the
  RenderSystem calls. Emitting from it means a native `_setPass`/`renderSingleObject`/automatic
  parameters/RTSS over the snapshot: path 1 in full, over copies, with the queue order and the
  pass sorting reproduced. The copy itself is not the problem (the producer already copies about
  as much per frame: constants, descriptions, changed buffer contents); the native emission is.
  A deep copy of the guest objects in their own layout (OGRE object graphs with MSVC `std`
  containers) is XL and buys nothing over a host-side description.

Guest memory itself is not the obstacle: it is readable from any host thread (the backend never
reads it, by design; the producer copies on the game thread), and a second guest thread is
allowed. The obstacle is that the emission's inputs are the simulation's outputs, live, with no
double buffering anywhere in the game.

So the pipeline that is possible already exists, below the boundary: producer on the game thread,
backend on its own, the frame queue and the backpressure work of 2026-10-10/11. Moving the
boundary one level up, to a snapshot of the render queue emitted natively, is candidate F2 of
section 3 with a concrete shape: it would take the whole 40-45 % off the thread (about 1.6x when
the thread is the limit, the only large step available), at XL size and high risk, and it is the
direction the legacy took (9.5). Not proposed; recorded so that the trade is explicit.

### 9.4 Ranking of this section

| # | Item | Gain (thread) | Risk | Size | Verification |
|---|---|---|---|---|---|
| 1 | 9.2 per-object frustum test in the walk hook | 1-2 % | L | S | replays identical, like the bucket culling |
| 2 | 9.1 class B binds without the device-resource lookups | 1.5-2.5 % | L-M | S-M | session recordings: commands identical |
| 3 | 9.1 native `_setTextureUnitSettings` and `_setTextureBlendMode` | 1.5-2.5 % | M | M | same, plus the member write watch |
| 4 | 9.1 native RTSS per-object listener | 2.5-3.5 % | M | M-L | same; constants compared in shadow mode |
| 5 | 9.1 native `_updateAutoParams` | 2-3 % | M | L | same |
| 6 | 9.2 producer phase B | 1.5-2 % | L | M | measured already; waits for a cooler machine |
| 7 | 9.2 backend computes lighting, fog and materials from sources | up to 6 % (overlaps 4 and 5) | H | L | new captures and replays |
| 8 | 9.2 render texture reuse when unchanged | 0-10 %, unknown | M-H | M | first a replay count of identical consecutive frames |
| - | 9.1 native `_setPass`, `renderSingleObject`; 9.3 pipelining the guest's emission | | H / not viable | XL | |
| - | F2: native emission over a render queue snapshot | ~40 % (1.6x) | H | XL | the legacy's road (9.5) |

Items 1-5 add up to 8-12 % of the thread and are independent of section 5's candidate; items 1
and 2 are the cheapest starts of either plan.

### 9.5 The legacy, and why it was abandoned

As far as its documents show (`~/torchlight-agent-worktree`, tag `legacy-detached-ogre`, 365
commits, `src/native_ogre/README.md`, `docs/re/*`, `docs/alpha/v0.1.0-alpha-readiness-audit.md`):

- **Where it cut.** At `D3D9RenderSystem::_render` (plus the world, view and projection setters and
  the unit-0 texture setter), decoding each draw from guest memory on the host at draw time: the
  vertex backing at `HardwareVertexBuffer+96`, the declaration repacked to POSITION FLOAT3, DIFFUSE
  bytes and TEXCOORD FLOAT2, the `[vertexStart, +vertexCount)` slice, one texture by name.
- **How it decided what to draw.** By admission per draw: single stream, non-indexed triangle
  lists, one texture unit; "refusal audits" for indexed state; UBYTE4 "gates"; draws identified by
  vertex and index counts (`1111v/4098i`, `36v/54i`) and hashes; a "production admission" list.
  Everything outside was refused and left to the Xenos picture underneath. Hence this project's
  first hard rule: generic by capability and state, never by signature.
- **What it rendered with.** A host OGRE 1.6 GL path, fixed function, in `gnu++03` with
  `-fpermissive`, switched by environment variables (`TORCHLIGHT_NATIVE_MODE=ogre-detached`,
  `TORCHLIGHT_DETACHED_*`). No programs: the guest's shaders are RTSS HLSL, and the fixed-function
  host could not express them; no UI projection, no atlas, no indexing, no generic resources
  ("No UI projection, atlas, shader, indexing or generic resource support was added").
- **How the work went.** Probes and evidence packages (menu evidence, blend value probe, font
  probe, CEGUI probe, colour chain, framebuffer read-backs), 59 "objective 1" checkpoint packages,
  audits of audits. After that, the main menu was still "visually broken" (UI invisible or
  sub-pixel), gameplay never got past single-draw proofs of concept (experiments 4 and 5), and the
  alpha audit parked the renderer in full
  ("NATIVE RENDERER R&D — parked in full ... production admission unchanged"). The game shipped
  its alpha path on Xenos.
- **What was kept.** The sync (`detached_sync.cpp`: only `sub_821F3D78` with `r7 == 1` owes
  retirement; the queue publication at `device+11456`), the `+96` backing store, the `_render`
  guard address, the common `_render` address. All re-verified before use (`guest_abi/`).
- **Why it failed, read from the above.** It cut too low (one call, `_render`, without the state
  model around it), it chose draws instead of implementing states, it never had a replay (every
  check was a live probe), its host side could not express the guest's programs, and the method
  (probe, package, audit) consumed the effort. The rewrite kept the boundary idea but moved it to
  the whole RenderSystem surface with a complete, serializable command stream, a host OGRE 14
  backend with generic state tracking and generated programs, replay from captures, and
  diagnostics behind one flag. Any F2 attempt would face the same two walls the legacy hit, now
  one level up: expressing the guest's pass logic natively, and validating it without a replay of
  the guest's side; the second wall is lower today (session recordings exist), the first is not.

### 11.2 The layout update of the fight, read only (2026-10-14)

`0x82198878` (7.3 % of the thread in the scripted fight, 2.1 % in the town square) is the level's
per-frame update of its layouts: for each `CLayout` (slot 64, `0x821B7650`: an enabled byte and a
virtual check), each `CLayoutScene`/`CEditorScene` (slot 64, `0x821B7588`) walks its objects
(the vector at `+240`, with Runic's `dcbt` prefetches: they knew it is memory bound) and calls
each descriptor's slot 7 with `dt`; `CLayoutDescriptor` (`0x821B7510`) recurses into its children.
Where the time goes (fight, inclusive): `CGenericModelDescriptor` slot 7 `0x821DEDF8` 4.2 %, the
two walks' own loops 2.0 % (self: the vtable load of every object misses the cache), room pieces
0.35, sound objects 0.27, logic timers 0.13, triggers 0.04. `CGenericModelDescriptor::Update`
walks the model's children, `dynamic_cast`s each one (`0x821E1828`, two type descriptors) and,
for those that are animation players, calls `0x821BB578` with `dt`: 4.0 % of the thread, all of it
the animation player update and the bone evaluation it forces (`Entity::_updateAnimation`). So the
fight's extra over the town is the dungeon's animated props (the mine's decor: generic models with
skeletal animations) having their bones evaluated every frame by the layout update, on screen or
not; in the town the animation players belong to the units (`CCharacter` slot 83, 10.6 %) and
the props are few.

Is it repeated work? The walk itself repeats the same vtable loads over thousands of static
objects each frame (2.0 %), and the animation players advance their states and force the bone
evaluation for every animated prop regardless of visibility, while OGRE would evaluate the bones of
a visible entity lazily at `_updateRenderQueue` and never those of an invisible one. Two isolable
proposals, neither implemented:

- **Skip the forced bone evaluation of a layout prop not rendered last frame** (hook at the
  animation player's call to `Entity::_updateAnimation` `0x821B9478`, deciding by the entity's
  last rendered frame, which OGRE keeps): the picture is unchanged for visible props (the render
  path evaluates them), and the time position still advances (the player writes the animation
  states before the call). Gain up to ~3 % in dungeons, ~0 in the town. Size S-M. Risk M-H: it
  changes behaviour for anything that reads an off-screen prop's bone or attachment transforms
  (`TagPoint`s: lights or effects attached to a bone), which a replay cannot show; it needs a
  reading of what reads those transforms before anything else.
- **The walk's loops as host code** (two functions, the vtable call per object left to the guest):
  the self time is the cache misses on the objects, which a host loop pays too (the controller
  walk experiment of 2026-10-09). Not proposed.

What the doc's candidate A (section 3) said about the game-update path is therefore more
specific now: in dungeons, about half of the forced evaluations are props, not units.

### 11.1 The blind hand test (ready, not run)

`out/blind/` of the worktree `~/tl-fable` (local, not in the repository): `setup.sh` copied the
base build (develop `df10a44` with the frame-pointer option only) and the branch's build at step
4 (steps 3 and 4; step 5 off) to `out/blind/A` and `out/blind/B` in a random order, the key sealed in
`out/blind/.key`; `run.sh A 1` (then `B 1`, `A 2`, `B 2`...) does the cool-down, a fresh copy of
the fixed-floor save with the measurement settings (no cap, no vsync, 720p, NVIDIA) and launches
that build by hand: the reference route (Continue, the mine standing still, the fight west over the
bridge, the stairs up, the town square round), F3 for the two frame rates, the log's frame time
summaries every 10 s kept under `out/blind/runs/`; `reveal.sh` opens the key afterwards. Expected
from the profiles: about 4-6 % less game-thread CPU per frame on the branch, at this laptop's
noise level by hand (3-10 % between runs), so alternate and repeat.

### 11.3 The blind hand test, done (2026-10-14, 11:30-11:50)

Base (develop `df10a44`) against the branch at step 4 (steps 3 and 4), both built with the render
agent's local step overlay commits (not pushed), Release, no input scripts; four runs A B A B on
fresh copies of the fixed-floor save, no cap, no vsync, 720p, the 55 C cool-down before each; the
user played the reference route and gave impressions before seeing numbers. Game frame rate per
step (the overlay's marks), with the mean CPU clock of the step (the package at 82 C in every
run):

| Run | Dungeon, still | Dungeon, fight | Town square | CPU MHz (still / fight / town) |
|---|---|---|---|---|
| A 1 (base) | 224.0 | 178.2 | 141.5 | 3136 / 3253 / 3102 |
| B 1 (branch) | 249.0 | 184.1 | 148.3 | 3653 / 3191 / 3278 |
| A 2 (base) | 215.3 | 168.8 | 138.4 | 3033 / 3027 / 3138 |
| B 2 (branch) | 231.6 | 174.5 | 148.2 | 3184 / 3099 / 3244 |

Both branch runs beat both base runs in every step: standing still +7.6 to +11 %, the fight +3.3 to
+3.4 %, the town square +4.8 to +7.1 %. The user's impressions, written before the numbers: "B felt
faster" after B 1, "still the same opinion" after A 2. The branch's runs also had the slightly
higher clocks (B 1 standing still at 3653 MHz against 3136), so part of the gap is thermal luck;
the fight's +3 % is the floor of what the profiles predicted (steps 3 and 4: -4.3 / -5.6 % CPU per
frame) and the town square's +5-7 % matches it.

## 10. Open

- A profile with usable call chains (town square and fight), coordinated with the render agent:
  the inclusive shares of candidates A, B and C, the game logic's remaining ~8 % in the fight,
  and the A* share in fights.
- Confirmation of the hypotheses marked above: `0x821BB578` as the unit's animation player
  update, `0x821E5910` as the level update, whether dynamic blockers write the walkability grid,
  and whether anything in combat reads bone transforms.
- The attribution tools built for this study (the all-classes RTTI vtable map, the static call
  graph, the function-to-subsystem vote) live outside the repository; they reuse
  `tools/guest_re/` and can be added there if the method is kept.
- Section 9: `0x82581D98` is `std::map::find` shared by many callers (section 11, step 2); the
  buffer lookups feed the uploads our producer reads; count, in a replay of the reference route, how often two
  consecutive frames of the light map and the shadow textures are identical (9.2); confirm which
  automatic constant types the game's programs use (the captures' program sources).

## 11. Execution log (2026-10-14, branch `perf/game-thread-native`)

Gate of every step: the 90 unit tests, the 20 parity captures replayed byte for byte, a live
recording of the stretches without randomness (menu, the fixed-floor load standing still, the town
after `ASCEND` standing still) compared command by command with the base's, and a frame-pointer
profile of the scripted route against the base's (`out/runs/` of the worktree: `run.sh`,
`gate.sh`, `fold.py`, `step_cpu.py`; the render agent's `sescmp` for the recordings).

| Step | What | Gain (game thread) | Identical | State |
|---|---|---|---|---|
| 0 | Frame-pointer profiling build; fight and town profiles with call chains | - | - | done (section 2.4) |
| 1 | Per-object frustum test in the walk hook (`MovableObject::isVisible` for the walk's call) | none: 0.0 objects dropped per walk, 22-24 tested, dungeon and town | yes (nothing dropped) | reverted |
| 2 | Class B binds without the device-resource lookup | not done | - | deferred (below) |
| 3a | `_setTextureBlendMode`'s member writes as host code (`--native_render_system_members`) | -1.0 (fight) / -1.1 (town) points of the thread: the guest function gone, nothing grew | yes: the 32 bytes byte-identical over 3,000,000 calls in a verification build; same 35 distinct blend states recorded | kept |
| 3b | `SetTextureBlend` records only the fields its operation reads (an early-out fix found by 3a) | the records of that type per draw 8.0 -> 1.85 (-77 %); producer and backend work with them | yes: the same 35 masked states; every other command type within the two-base spread | kept |
| 3 | Game thread CPU per frame, base -> steps 3a+3b (profile runs) | fight 5.09 -> 4.98 ms (-2.2 %), town 5.63 -> 5.39 ms (-4.3 %); fps 196 -> 201, 177 -> 185 | | |
| 4 | RTSS listener: the SGPass lookup and type check as host code (`--native_rtss_listener`) | listener subtree 5.0 -> 3.5 (fight), 4.6 -> 2.9 (town) points; the host lookup 0.1-0.9 of that: net about -1 point | yes: 2,000,000 calls, none left to the guest; replays and tests unchanged; the still captures' draw sets differ no more than two base runs' | kept |
| 4 | Game thread CPU per frame, base -> steps 3+4 | fight 5.09 -> 4.87 ms (-4.3 %), town 5.63 -> 5.31 ms (-5.6 %); fps 196 -> 205, 177 -> 188 | | |
| 5 | `_updateAutoParams` replayed as host code (`--native_auto_params`): the dispatch and the copies host, every getter the guest's | auto params subtree 1.6 -> 1.1 (fight), 2.5 -> 0.8 (town) points in one run each; CPU per frame of the thread: no gain shown (base -> 3+4: -4.3 / -5.6 %; base -> 3+4+5: -3.1 / -4.2 %, one run each, within the spread) | yes: 6,000,000 updates, none left to the guest; the captures' constant checks and command types unchanged | in the branch as its own commit, off by default: no demonstrated gain |
| 3+4 | Game thread CPU per frame, base -> the kept steps | fight 5.09 -> 4.87 ms (-4.3 %), town 5.63 -> 5.31 ms (-5.6 %) (profile runs, one each; the laptop's run-to-run spread is about 2 %) | | the blind pair |
| 6 | Skeletal animation, shadow mode | not started | - | suspended by the user: only 2-4 % without a parallel tier (section 2.4); after what comes of 11.2 |

**Step 1, why nothing.** The hook worked as designed (every object of a visible node was tested
against the planes with its cached world box), and dropped none in 2,400 walks of the dungeon
and the town. The game attaches each movable object to its own scene node, so the node box the
walk already tests is the object's box: the test is redundant. The off-screen non-bucket draws
of `docs/guest-hot-paths.md` (1-14 % of the former off-screen set) are objects whose box touches
the frustum while their geometry does not; no box test catches them. Kept in the history as a
reverted commit; `docs/guest-hot-paths.md` unchanged.

**Step 3, what happened.** The host `_setTextureBlendMode` (hooks/native_render_system.cpp)
writes `mManualBlendColours[unit]` exactly as Runic's build does (the colour type stores its two
colour arguments at `+144 + 32 * unit`; the alpha type stores nothing; the function has no device
call). The first recordings with it showed 12 % more `SetTextureBlend` records per draw than the
base's (9.0 against 7.7-8.3 in four guest-path recordings), reproducible (two runs), with the same
textures, constants and programs per frame. The cause was not the member writes (a local
verification build ran both paths and compared the bytes: 3,000,000 calls, 0 differences) but what
the hook records: OGRE 1.7 never initializes `LayerBlendModeEx::alphaArg1/2` and `factor` (nor the
colour arguments of an alpha-type mode), so the hook was recording stack leftovers, which change
with whatever ran before the call, and the early out (`Session::Unchanged`) took them for
changes. Decoded, the extra "states" were alpha modes differing only in those words. The fix
(3b) records the colour arguments only when a source is `LBS_MANUAL` and the type is colour, the
alpha arguments likewise for the alpha type, and the factor only for `LBX_BLEND_MANUAL`, in the
early-out words and in the command; the backend keys its programs on the arguments under the same
conditions and passes the factor to the RTSS as it comes (the render agent confirmed the shape and
noted the factor is not part of its program key: a latent backend question, no operation 12 in a
whole town session). With it, the records of that type per draw fall from 8.0 to 1.85 and two
recordings of the same scene agree. The render agent was told, since it affects every session
comparison made so far.

**Step 4, what happened.** The listener's own code was glue: a string-keyed `std::map` lookup
(`UserObjectBindings::getUserAny("SGPass")`, with `std::string::compare` per node), an `any_cast`
through the RTTI, then the loop over the render state's sub render states. The host version reads
the same guest objects (the pass's bindings, the map's tree, the `Any`'s holder, whose vtable must
be `Any::holder<SGPass*>`'s, the `SGPass` and its render state) and calls the guest's sub render
states with the same arguments in the same order (`rex::runtime::ResolveIndirectFunction`, the
targets cached). Everything the lookup does not recognise runs the guest. The first version cost
0.6 % of the thread itself (string copies and per-call resolves); with string views into guest
memory, the key read once and the targets cached it costs 0.1-0.9 (the attribution splits it
between the listener marker and "hooks"). What remains of the subtree (3.5 / 2.9 %) is the sub
render states' own math: `FFPLighting` 0.5, the matrix product `0x821C2CC8` 0.5-0.6,
`_writeRawConstants` 0.4, `FFPFog` 0.3, `FFPTexturing` 0.2: candidate 9.1 row 3 proper, a port of
their parameter updates, not done.

**Step 5, what happened.** The guest's `_updateAutoParams` is a 126-case switch over the
program's entries (type, physical index, element count, data, variability), each case a call to
a data source getter and a copy into the float buffer (`0x821C2868` for matrices, transposed when
`mTransposeMatrices` is set, at most 16 floats; `0x82467DE0` for colours, at most 4; 48 bytes per
matrix for the 3x4 array; the derived light diffuse as four single multiplies; the light position
through the guest's fused matrix-vector product `0x823FDEF8`). The host version keeps every
getter the guest's (called by vtable slot, by reference or by value into the guest frame's own
slots, the float in `f1`), so the values are the guest's to the bit, and does the dispatch and the
copies itself; any entry of a type outside the 19 the programs use hands the whole update to the
guest (none did in 6,000,000 updates). The gain would be the recompiled dispatch and copy loops:
the subtree lost 0.6-1.7 points of the thread in one run each, but the thread's CPU per frame
came out no better than without it (one run each; the laptop's spread is about 2 %), so there is
no demonstrated gain and the step adds a hook that calls guest code with its own conventions.
The user's decision: it stays in the branch as its own commit with `--native_auto_params` off by
default, outside the blind pair. To defend it: several interleaved runs (base, 3+4, 3+4+5),
compared by the subtree's CPU time and by the thread's CPU per frame, not one run each. The gate's "commands identical" rests
here on the captures' constant checks (the same match and mismatch counts as step 4, the
tool's own pre-existing mismatches included) and the per-frame command types, since two live
runs never give equal constant values (camera and animation move).

**Step 6, suspended by the user.** The game's RTSS programs use 17 automatic constant types (from the 42 program sources of the captures: `worldviewproj_matrix`,
`viewproj_matrix`, `world_matrix`, `view_matrix`, `inverse_world_matrix`,
`inverse_transpose_world_matrix`, `inverse_transpose_worldview_matrix`, `world_matrix_array_3x4`,
the four `surface_*` colours, `derived_scene_colour`, `derived_ambient_light_colour`,
`derived_light_diffuse_colour`, `light_position_view_space`, `light_diffuse_colour`,
`texture_matrix`), so a host `_updateAutoParams` can cover them and leave a program with any other
type to the guest (generic by capability). Its guest share is 4.6 % (`_updateAutoParams` 0.7 self,
the `AutoParamDataSource` getters 1.0, `_writeRawConstants` 0.6, the matrix product 0.5, `fma`
0.3). The output is the parameter buffer our producer reads, so the gate is "commands identical"
with the recordings: the matrix products must reproduce the guest's fused operations in order
(`std::fma` where the recompiled code calls it). For the skeletal animation, section 2.4's finding
stands: the game update forces every evaluation (`CAnimationPlayer::Update` 10.8 % inclusive in
the town square), so the plan's parallel tier has nothing to overlap and the shadow-mode port gives
its speedup only (2-4 %).

**Step 2, why deferred.** The per-device lookup `0x82581D98` is a `std::map::find` shared by many
callers; in the fight it is 1.28 % of the thread: `_render` 0.41 (the index buffer's device copy),
`setVertexBufferBinding` 0.35 (the vertex buffers'), `D3D9Texture::getTexture` 0.27 (under
`_setTexture`) and `bindGpuProgram` 0.15. The buffer lookups are not waste: they find the device
copy that the guest then uploads from the system-memory shadow (`0x821A71E8`) and that our
producer reads at the draw (`VertexBufferMemory` walks the same per-device map to the Xbox buffer's
fetch dwords: address, size and endian mode). Skipping them means the producer reading the shadow
(`+0x60`, `+0x58`) instead and knowing the fetch endian mode without a device buffer: a producer
change with its own validation, for at most 1.3 % of the thread. The texture and program lookups
feed device calls the user decided to keep (steps c and d). Not done tonight; left as a note for
the producer's owner.
