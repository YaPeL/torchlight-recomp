# Skipping the guest's D3D work in the native mode: investigation plan

Status: plan, nothing implemented. Every fact marked *hypothesis* comes from the OGRE 1.7.0 PC
sources (`OgreD3D9RenderSystem.cpp`, `OgreRenderSystem.cpp`) and has to be confirmed in the
recompiled code, which is Runic's build of that render system on the Xbox D3D runtime, before it
is used.

## Why

In the native mode (`--native_live=only`) every RenderSystem call the guest makes reaches our hook,
which records what the native backend needs, and then the guest's own implementation
(`__imp__sub_...`), which programs the Xbox D3D device: render and sampler states, shader constants,
stream sources and draw packets for a Xenos GPU that the `null` GPU plugin discards
(`patches/README.md`, patch 8). A profile of the main thread (`docs/performance-profile.md`, "The
guest's memory copies on the host") puts the frame rate drops below 100 fps on the number of draws
(about 340 per frame against 180) at a constant cost per draw (33-37 us), with at least 12 % of the
main thread in the guest's own D3D9 `_render` under our hook, and more in the state setters (for
example `_setTextureUnitSettings`, `0x821C93A0`, about 9 % inclusive). A captured frame of the
fixed-floor dungeon (182 draws) makes about 8,200 RenderSystem calls; 4,600 of them set texture
stage and sampler state.

Goal: in the native mode only, do not run the guest's implementation of a RenderSystem call when its
only effect is on the Xenos device, keeping every effect the guest itself can observe.

## Hard constraints

- Xenos (`--native_live=off`) and `parallel` do not change at all: there the Xenos plugin draws what
  the guest's D3D builds. The decision is made once at startup from the mode; outside `only` every
  hook calls `__imp__` exactly as today.
- Generic: by slot and by state, never by draw, mesh, material or texture.
- Our recorded commands must not change: the hook body runs before `__imp__` and reads the guest's
  OGRE objects, not the device. A skipped call that changed what the guest does next would show up
  as a different command stream (see Validation).
- One cvar, off by default (`native_skip_guest_d3d`, see Decisions).
- No probe or trace without agreeing on it first (each one this plan needs is listed under Evidence).

## What can observe a skipped call

For each slot the guest implementation can leave state in four places. The plan has to find, for
each, every reader.

1. **The render system object** (`D3D9RenderSystem` and its `RenderSystem` base). Members written by
   the setters and read by getters or by other guest code: `mCullingMode` (`_getCullingMode`,
   slot 62, called 154 times per frame), `mTexStageDesc[]` (read by `_setTextureMatrix` and
   `_setTexture`), `mActiveVertex/FragmentGpuProgramParameters`, `mVertexProgramBound`
   (`isGpuProgramBound`, slot 94), `mLastVertexSourceCount`, `mViewMatrix`, `mDxViewMat`,
   `mDxProjMat`, `mDxWorldMat`, `mClipPlanesDirty`, and the counters `mBatchCount`, `mFaceCount`,
   `mVertexCount` (`_getBatchCount` and friends, slots 71-73) that the base `RenderSystem::_render`
   (`0x821CEA60`) updates.
2. **The D3D device's shadow state.** OGRE's `__SetRenderState` reads the device's current value
   back (`GetRenderState`) and skips equal ones (*hypothesis* for the Xbox build). Skipping the
   setters of a state and its read-back together keeps them consistent; skipping some writers of a
   state but not others would not.
3. **Resource tracking in the Xbox D3D runtime.** A draw marks the resources it uses (vertex and
   index buffers, textures, shaders) with the GPU fence of the batch; a later lock of the resource
   waits for that fence. Skipping draws would stop those waits (harmless with the `null` GPU, which
   never reads the memory), but it changes timing: to be measured, not assumed.
4. **The command ring.** Fences (`EVENT_WRITE*`, `MEM_WRITE`), interrupts, `WAIT_REG_MEM`, the swap
   and vblank still have to flow: the guest waits on them (`hooks/gpu_wait_hooks.cpp`,
   `sub_821F3D78` with `r7 == 1`). Draw and state packets do not carry any of those; the frame end,
   swap and explicit fences do. Those slots stay untouched.

## Slot by slot

Calls per frame from the fixed-floor dungeon capture (`check-1`, 182 draws). Class: **A** the
implementation only programs the device (skip candidate); **B** it also writes render system members
(skip only the device part: a host implementation that writes the same members, proven member by
member); **C** keep (lifecycle, frame, targets, sync, queries, getters, resources).

| Slot | Function | Calls | What OGRE 1.7 D3D9 does (*hypothesis*) | Guest-readable state | Class |
|---|---|---|---|---|---|
| 44 | `_setTextureUnitFiltering` (one filter) | 915 | sampler state | device sampler state | A |
| 45 | `_setTextureUnitFiltering` (min/mag/mip) | 305 | calls slot 44 three times (confirmed) | none of its own | A |
| 43 | `_setTextureBlendMode` | 610 | render and texture stage states | device states | A |
| 47 | `_setTextureAddressingMode` | 305 | sampler state | device | A |
| 46 | `_setTextureLayerAnisotropy` | 277 | sampler state | device | A |
| 49 | `_setTextureMipmapBias` | 277 | sampler state | device | A |
| 81 | `_setPolygonMode` | 285 | render state | device | A |
| 51 | `_setSceneBlending` | 126 | render states | device | A |
| 53 | `_setAlphaRejectSettings` | 126 | render states | device | A |
| 64-67 | depth check, write, function; colour write | 126-154 each | render states | device | A |
| 68 | `_setDepthBias` | 126 | render states | device | A |
| 36-37 | point sprites, point parameters | 98 each | render states | device | A |
| 41 | `_setTextureCoordSet` | 305 | stage state | `mTexStageDesc[].coordIndex` | B |
| 42 | `_setTextureCoordCalculation` | 305 | stage state | `mTexStageDesc[].autoTexCoordType`, `.frustum` | B |
| 50 | `_setTextureMatrix` | 305 | transform and stage state; reads `mTexStageDesc` | device | A once 41/42 keep their members |
| 39 | `_setTexture` | 313 | `SetTexture`, sampler and stage states | `mTexStageDesc[].pTex`, `.texType`, `.coordIndex`, `.autoTexCoordType`; texture resource tracking | B |
| 40 | `_setVertexTexture` | 285 | `SetTexture` on a vertex sampler | `mTexStageDesc[].pVertexTex` | B |
| 32 | `_setTextureUnitSettings` | 277 | base class: calls the slots above per unit | members through them | keep; its callees decide |
| 34, 33 | `_disableTextureUnitsFrom`, `_disableTextureUnit` | 126, 8 | `_setTexture(0)` per unit | as slot 39 | B |
| 91 | `bindGpuProgramParameters` | 308 | shader constants to the device | `mActive*GpuProgramParameters` | B |
| 90, 93 | `bindGpuProgram`, `unbindGpuProgram` | 196, 56 | `SetVertexShader`/`SetPixelShader` | `mVertexProgramBound` etc. (slot 94 reads them) | B |
| 84 | `setVertexDeclaration` | 182 | `SetVertexDeclaration` | device | A (check resource tracking) |
| 85 | `setVertexBufferBinding` | 182 | `SetStreamSource` per stream | `mLastVertexSourceCount`; buffer tracking | B |
| 87 | `_render` | 182 | base `_render` (counters, clip planes, `setClipPlanesImpl`) then `SetIndices` and the draw | counters, `mClipPlanesDirty`; fences on resources | B, last |
| 61 | `_setCullingMode` | 131 | render state | `mCullingMode` (slot 62) | B |
| 28, 30, 31 | world, view, projection matrices | 28-33 | `SetTransform` | `mDxWorldMat`, `mViewMatrix`, `mDxViewMat`, `mDxProjMat`, `mClipPlanesDirty` | B |
| 55, 58, 59, 107, 114 | frame begin/end, viewport, clear, render target | 5 | EDRAM, resolves, present path | targets, EDRAM bookkeeping, swap | C |
| 101, 102 | update and swap all render targets | 1 | frame loop | frame loop | C |
| 4, 5 | occlusion queries | 0 here | device queries | results read by the guest | C |
| 112, 113, 62, 72, 94 and the other getters | | | members only, no device | | C (cheap) |
| hardware buffer lock/unlock, texture creation | | | resources | the guest's data | C (out of scope) |

The table is the starting hypothesis; the Evidence step rewrites each row from the recompiled code
before anything is skipped.

## Evidence that a skip is safe

For each slot, in this order, and recorded in `guest_abi/` next to the slot (address, evidence) and
in this document:

1. **Read the recompiled implementation** (`__imp__sub_<addr>`, `generated/default`): every store
   and its target (`this+offset`, the device, globals), every call and virtual call. Name each
   target against the 1.7 header layout (`guest_abi/ogre_layout.h`); differences are Runic
   differences.
2. **Find every reader of each member it writes**: loads of `this+offset` from the render system's
   vtable functions and from the SceneManager functions that hold the render system pointer
   (`0x821C5730` `_setPass`, `0x821C5FA0`, per the legacy; to be confirmed). A member nobody reads
   needs nothing; a member something reads has to be written by the host implementation exactly as
   the guest wrote it.
3. **Device state read back**: find the Xbox D3D functions that read the state the slot writes
   (`GetRenderState`-like reads, dirty masks consumed at draw time). Skip a state's writers and its
   readers together, or not at all.
4. **Resource tracking and waits**: find where a draw or a `SetTexture`/`SetStreamSource` stores a
   fence or reference in the resource, and every lock path that waits on it (`0x821A70D8` and the
   hardware buffer `lockImpl`, `guest_abi` `hardware_buffer_vtable`). Losing those waits is accepted
   (Decisions); the step records where they are, for the day a deferred read would need them.
5. **Dynamic confirmation** (diagnostics build only, `TORCHLIGHT_DIAG_WRITE_WATCH`): a write watch
   on the members of step 2 for one session of the fixed-floor scene and the menus, to catch a
   reader the static reading missed.

## Order of the experiments (lowest risk first)

1. Class A sampler and texture stage states (slots 44-47, 49, 43): the bulk of the calls, device
   only, no members.
2. Class A render states (51, 53, 64-68, 81, 36, 37), skipped together with their read-back (step 3).
3. Class B texture stage (39-42, 50, 33, 34): host implementations that write `mTexStageDesc`.
4. Shader constants and programs (91, 90, 93): members kept, device calls skipped.
5. Transforms and culling (28, 30, 31, 61).
6. Declarations and streams (84, 85).
7. `_render` (87): last; the base counters and clip planes are kept, only `SetIndices` and the draw
   are skipped.

Each step is its own commit, behind the same cvar, and is measured before the next one starts.

## Validation of each step

1. Unit tests: the decision (mode, slot) as a pure function; host implementations of class B slots
   tested against member layouts with a fake guest memory, like `guest_copy_test`.
2. The 20 replays unchanged (they do not run the guest; they guard the backend).
3. Same commands: a live session recording (`--live_record`) of the fixed-floor saved game and of the
   menus, with the skip on and off, replayed with `replay --session`: the commands of every frame
   identical apart from timing. A different command means the guest saw the skip; the step is
   reverted.
4. Xenos unchanged: the code path is not taken outside `only` (a unit test of the decision), plus one
   Xenos run to the dungeon and an F9 capture compared with one from before the change.
5. Performance: the fixed-floor runs (`docs/performance-profile.md`), with the cvar on and off, same
   binary, focusing on the two slow stretches (dungeon fight with the NPC, town walking).
6. Soak, once per class: new character, level loads up and down, town portal, menus, inventory,
   a cutscene, saving and loading, quitting to the dashboard; no new errors in the log.

## Decisions (2026-10-07)

- **Cvar**: `native_skip_guest_d3d` (bool, default `false`): in the native mode, the RenderSystem
  calls whose only effect is the Xenos device do not run the guest's implementation. It stays off
  by default until the soak of every class it covers passes; the log's first lines say its value.
- **Diagnostics build** (Evidence step 5): approved, behind a CMake option off by default that
  neither the release nor any preset turns on, like `TORCHLIGHT_DEV_COMMANDS`:
  `TORCHLIGHT_DIAG_WRITE_WATCH`.
- **Resource fences** (Evidence step 4): accepted, the waits go with the skipped draws and
  bindings. The producer copies everything it sends on the game's thread, at the bind or the draw,
  before the guest's implementation runs: `Session::RecordContent` (`capture/session.cpp`) copies
  the bytes into the snapshot store when the content's version changed, and the version only moves
  on the guest's unlocks (`Session::OnUnlock`, `OnContentWritten`); named textures are loaded by the
  backend from the game's files, not read from guest memory; the backend thread only gets those
  copies. No read of guest memory is deferred, so a guest write after the draw, which the fence
  would have held back for the GPU, cannot change what was sent. If a deferred read is ever added,
  the fence tracking of `_render` and the bindings comes back for those resources.

The first step is a (sampler and texture stage states of the device), on its own branch, with the
whole evidence of this plan written before any code.
