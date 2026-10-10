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
| 45 | `_setTextureUnitFiltering` (min/mag/mip) | 305 | calls slot 44 three times (confirmed) | none of its own | keep: our slot 44 hook records through it |
| 43 | `_setTextureBlendMode` | 610 | render and texture stage states; in the guest, members only (step a) | render system members at `+144 + 32 * unit` | keep |
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

## Step a: evidence (2026-10-07)

Recompiled code read for slots 43-47 and 49 and every helper they call; device offsets are from
the device pointer in the global `0x8355A2E4` (`guest_abi` `xbox_d3d::kActiveDeviceGlobal`).

- **The Xbox D3D keeps sampler state inside the device's texture fetch constants**, at
  `device + 1152 + 24 * sampler` (six words per sampler), marks them in the dirty mask at
  `device + 24` (bit `32 + sampler`), and keeps the maximum anisotropy per sampler in the byte at
  `device + 10864 + sampler`.
- **Slot 44** `_setTextureUnitFiltering` (`0x821CA470`): reads the stage's texture type
  (`this + 24 * (unit + 37)`, `@0x821CA4A8`) and the caps (`this + 2184`), maps the filter
  (`0x821CA528`) and calls `0x821CA620`, OGRE's `__SetSamplerState`: it reads the current value
  through the device's getter table (`device + 952 + type`, `@0x821CA648`) and, when different,
  writes it through the setter table (`device + 468 + type`, `@0x821CA66C`). On failure it throws
  (`@0x821CA510`). No store to the render system. **Device only.**
- **Slot 45** (`0x821CA3F0`): three virtual calls to slot 44 (`vt + 0xB0`, `@0x821CA420`,
  `@0x821CA440`, `@0x821CA460`), nothing else. All 915 calls of slot 44 per frame come from it
  (3 x 305). Our slot 44 hook records the filters (`SetSamplerFilter`) and slot 45's only counts,
  so **slot 45 keeps running**: skipping it would drop those commands.
- **Slot 46** `_setTextureLayerAnisotropy` (`0x821CAD08`): clamps to the caps' maximum
  (`@0x821CAD24`), reads the device byte (`0x821CAD78`) and writes it with `0x82768830`, which
  also rewrites the sampler's fetch constant filter bits and sets the dirty bit. **Device only.**
- **Slot 47** `_setTextureAddressingMode` (`0x821CA1A8`): for U, V and W reads the current clamp
  bits of the fetch constant (`0x821CA3D8`, `0x821CA190`, `0x821CA3C0`) and, when different,
  writes them in place (`stwx` `@0x821CA264`, `@0x821CA304`, `@0x821CA3A4`) and sets the dirty bit
  (`@0x821CA270`, `@0x821CA310`, `@0x821CA3B0`). **Device only.**
- **Slot 49** `_setTextureMipmapBias` (`0x821CA880`): when the caps allow it (`this + 764`,
  `@0x821CA8A0`), reads the bias from the fetch constant (`0x821CA8F0`) and writes it with
  `0x82768940`. **Device only.**
- **Slot 43** `_setTextureBlendMode` (`0x821C9100`): no device call at all; it stores the blend
  colours in the render system, `this + 144 + 32 * unit` (`@0x821C91A8`, `@0x821C91B0`,
  `@0x821C92BC`, `@0x821C92C4`): a Runic difference (OGRE 1.7 PC sets texture stage states). Not a
  candidate; it stays.
- **Other readers of that device state.** The getter table (`device + 952`) is used only by
  `0x821CA620`. The read-back helpers are called only by the slots above and by the `_endFrame`
  wrapper `0x821B1000` (`@0x821B1260`, `@0x821B1274`), which saves sampler 0's state, sets its own
  for a full-screen pass (`0x82198A08`) and restores what it saved (`@0x821B1438..@0x821B1490`):
  the values only go back into the device. The anisotropy byte is also read by `0x821AF998` and
  `0x821B6010` (filter setters, called only from that wrapper). Everything else that reads the
  fetch constants builds GPU packets, which the `null` plugin drops. Our producer reads none of
  it (`ActiveDevice` is only a key of the per-device resource maps).

Conclusion: in the native mode, slots **44, 46, 47 and 49** can skip the guest implementation;
43 and 45 keep it. What it is worth: in the slow stretch of the profile (dungeon fight, last
15 s) those four implementations take about 1 % of the main thread together (0.54 % of all
samples). The guest's D3D under the RenderSystem is mostly elsewhere: the D3D9 `_render`
(`0x821C4058`) about 5.8 % of all samples (about 12 % of the main thread), of which the draw
itself, after the declaration and stream bindings, about 3 %; `bindGpuProgramParameters` 0.7 %;
`_setTexture` 0.6 %. All of the guest's D3D work together is about 13 % of the main thread; most of
it is in the last step.

## Step a: implemented and validated (2026-10-07)

`--native_skip_guest_d3d=true` skips slots 44, 46, 47 and 49 (`hooks/guest_d3d_skip.h`,
`RECORD_HOOK` in `hooks/render_system_hooks.cpp`); the decision is tested in `guest_d3d_skip_test`.
Validation, same binary with the cvar off and on, the fixed-floor saved game, a session recording
(`--live_record`) and an F9 capture standing still where the player arrives:

- The 20 replays: byte for byte as before (the replay does not run the guest).
- Session recordings: the same sampler states sent (19 distinct in the menus, 22 in the dungeon,
  equal sets); the commands per guest frame equal within 1-3 % (the animation; the menu range mixes
  the title screen and the menu, which each run spent a different time on).
- F9 captures: the same 40 sampler commands; 185 against 184 draws (an animated effect); the two
  replays differ by 32.3 dB PSNR, where two runs before any change differ by 33.2 dB, and only in
  what moves (the pet, the character's pose, a glow): textures, filtering and addressing equal.
- Xenos: not run; the decision never skips outside `--native_live=only` (the test), and the log's
  first lines say what was decided.

Not measured: by the profile its gain is about 1 % of the main thread, below the noise of a run.

## Draw and bindings: evidence (2026-10-07)

The D3D9 `_render` (`0x821C4058`), in order: returns when the operation has no vertices
(`@0x821C4070`); calls the base `RenderSystem::_render` (`0x821CEA60`: batch counters, clip planes);
calls slots 84 and 85 through the vtable (`@0x821C4098`, `@0x821C40B4`), whose guest code uploads a
dirty buffer to its device copy (`0x821A71E8`, through the large copy); takes the device's render
target and depth stencil with a reference (`0x821C0A08`, `0x821E0088`) and releases them
(`0x821CEDD8`), with a path for depth-only passes that sets and restores render targets
(`0x821BA040`, `0x821D1288`, `0x821BF940`); for indexed draws finds or creates the device's index
buffer (`0x82581D98`, `0x82582520`), uploads it when dirty (`0x82582668`) and calls `SetIndices`;
then per pass iteration calls `_setDepthBias` through the vtable (`@0x821C43C8`), draws
(`DrawIndexedPrimitive` `0x821CF830` `@0x821C43F4`, `DrawPrimitive` `0x821D0A10` `@0x821C4490`)
and updates the pass iteration (`0x821CE928`); finally unbinds the streams and indices.

Our `_render` hook runs the guest's first and captures after it, because the buffer snapshots
read the device copy the guest has just uploaded, and our slot 84, 85 and 68 hooks record through
those calls. So `_render` is not skipped; its device calls are:

- **`DrawIndexedPrimitive`, `DrawPrimitive`**: flush the dirty device state into the ring
  (`0x821CFF38`), allocate ring space (`0x821EA6F0`), fences (`0x821F3D78`), wait for ring space
  only when the ring is full (`0x821A5C10`, 0.02 % of the samples in the slow stretch). Called only
  by `_render`.
- **`SetIndices`** (`0x821C39F8`), **`SetStreamSource`** (`0x821C3D58`): store the buffer in the
  device (`device + 12684`; `device + 4 * (stream + 3177)` and the stream's fetch constant), and
  stamp the replaced buffer's fence (`+8`) or queue a pending fence entry (`0x82775DF8`): the
  resource fences, whose loss is accepted (Decisions). Other callers: `setVertexBufferBinding`
  (`0x821C3E78`), the device's unbind-all `0x821CECF0` (from `_beginFrame`) and `0x827746B0`,
  which unbind; with every binding skipped there is nothing bound to unbind. No caller reads `r3`
  after them.
- **Kept**: the render target and depth stencil references (reference counts and EDRAM
  bookkeeping), `SetVertexDeclaration` (`0x821CE588`, two stores), the state flush when other
  draws call it (`0x821E94A0`, `0x82775198`: the `_endFrame` full-screen pass), the index buffer
  lookup and uploads.

Skipped since this step: those four device calls, by overriding them
(`hooks/render_system_hooks.cpp`, `DEVICE_SKIP_HOOK`), so every caller skips them. In the slow
stretch they took about 2.2 % of all samples (about 4.5 % of the main thread, a lower bound: the
call graphs are cut by the LBR depth).

## Draw and bindings: validated and measured (2026-10-07)

Same binary, cvar off and on, the fixed-floor saved game:

- The 20 replays: byte for byte as before.
- F9 captures standing still where the player arrives: the same 40 sampler commands; 183 against
  184 draws; the replays differ by 33.5 dB PSNR (two runs before any change: 33.2 dB), only in what
  moves.
- Session recordings (menus, dungeon, town): the same sampler states (33 distinct); the same draws
  per recorded frame in the dungeon, standing still (199.1 against 197.7). The live mode drops the
  frames the backend cannot keep up with (75-78 % in a recording run) and a recorded frame carries
  the presents of the dropped ones, so counts per present differ with the frame rate; counts per
  recorded frame do not.
- Xenos: not run (the decision never skips outside `--native_live=only`; the test).

Measured, two runs each, interleaved, frame rate per step (mean; 1 % low = the 99th percentile
frame time as a rate):

| Step | 1 % low off -> on | Frames over 33 / 50 ms (both runs) | Frame rate off -> on |
|---|---|---|---|
| Dungeon, fighting | 74.2 -> 79.7 fps (+7 %) | 4 / 2 -> 2 / 2 | 107.4 -> 116.3 fps (+8 %) |
| Town, walking | 70.2 -> 72.5 fps (+3 %) | 0 -> 0 | 93.3 -> 98.6 fps (+6 %) |
| Dungeon, still | 119 -> 126 fps | 1 / 0 -> 0 / 0 | 148.5 -> 163.6 fps (+10 %) |
| Town, still | 124 -> 136 fps | 0 -> 0 | 166.2 -> 177.7 fps (+7 %) |
| Main menu | | 0 -> 0 | 277 -> 271 fps (few draws: noise) |

In the fight, both runs with the skip beat both without on the frame rate (113.5, 119.1 against
105.7, 109.0) and on the 1 % low (78.6, 80.8 against 71.0, 77.5). The long frames did not change:
0-2 per run either way, spawn and load spikes, not the per-draw cost.

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

## Step b: class A render states, evidence (2026-10-10)

Recompiled code read for slots 36, 37, 51, 53, 64-68, 81 and 84, and every helper they call.
Device offsets are from the device pointer in the global `0x8355A2E4` (`guest_abi`
`xbox_d3d::kActiveDeviceGlobal`).

- **The render state setter** `0x821C5480`, OGRE's `__SetRenderState` (r3 = state, r4 = value).
  It reads the current value through the device's getter table (`device + 548 + state`, `bctrl`
  @0x821C54A8) and, when it differs, writes it through the setter table (`device + 64 + state`,
  @0x821C54C8). It always returns 0 (@0x821C54CC), so the exception paths after every call in the
  slots below (`0x821BE290` message, `0x8287D560` `OGRE_EXCEPT`) are never taken. No store to the
  render system or to any global.
- **Other readers of that device state.** The getter table at `device + 548` is called only by
  `0x821C5480`. The only other indirect call through a `+548` table entry in the image,
  `0x827DD920`, is through a COM-style object's own vtable (`*(r3) + 548`), not the device. The
  other callers of `0x821C5480` are slots 52, 61, 82, 83, 106 and 124 (separate blending, culling
  and stencil), which keep running; skipping some writers of a state only changes whether a later
  writer finds the device value equal. Nothing saves and restores render states the way the
  `_endFrame` wrapper `0x821B1000` does for sampler 0 (it does not call `0x821C5480`). Whatever
  else reads the device's render state builds GPU packets, which the `null` plugin drops.
- **Slot 36** `_setPointSpritesEnabled` (`0x821D2530`): state 184, 1 or 0 (tail calls
  @0x821D2544, @0x821D254C). **Device only.**
- **Slot 37** `_setPointParameters` (`0x821C56A0`): states 176, 180 and 188 (@0x821C56D0,
  @0x821C56E0, @0x821C570C). It reads the caps' maximum point size (`this + 764`, `+128`,
  @0x821C56F8) when the maximum passed is the default. No store. **Device only.** Its hook only
  counts today (`COUNT_HOOK`), so skipping it needs a `RECORD_HOOK` with an empty body.
- **Slot 51** `_setSceneBlending` (`0x821C52B0`): never reads `this`. ONE/ZERO sets state 60 to 0
  and goes to the blend operation; otherwise state 60 = 1, state 64 = 0, states 72 and 76 the
  mapped factors (`0x82201750`, a jump table) and states 80 and 92 the mapped operation
  (`0x821BF8B0`). Both mapping helpers are pure: no call, no store. **Device only.**
- **Slot 53** `_setAlphaRejectSettings` (`0x821C54D8`): states 96, 104 (the compare function
  mapped by the pure `0x821C2FA0`) and 100; with the alpha-to-coverage capability (`this + 764`,
  `+36` bit, @0x821C55E8) and the vendor at caps `+20`, state 176 with a vendor FOURCC
  (@0x821C5628, @0x821C5660). One store, the byte global `0x83582ADC` (@0x821C5694): OGRE 1.7's
  `static bool lasta2c`. Its only access in the image is that store (no load with that base and
  offset): a Runic difference, the comparison that read it was dropped. Skipping leaves a value
  nobody reads. **Device only.**
- **Slot 64** `_setDepthBufferCheckEnabled` (`0x821C2F38`): state 40. **Device only.**
- **Slot 65** `_setDepthBufferWriteEnabled` (`0x821C4E88`): state 48. **Device only.**
- **Slot 66** `_setDepthBufferFunction` (`0x821C3010`): state 44, the function remapped in place
  first (@0x821C301C..@0x821C305C; OGRE 1.7 maps it directly, so a Runic difference whose meaning
  the skip does not need) and then by `0x821C2FA0`.
  **Device only.**
- **Slot 67** `_setColourBufferWriteEnabled` (`0x821C3520`): state 212, the four channel bits.
  **Device only.**
- **Slot 68** `_setDepthBias` (`0x821D14B8`): states 208 and 204 when the caps allow
  (`this + 2184`, `+16`, `+72` bits 5 and 6). No store. **Device only.** The D3D9 `_render` calls
  it through the vtable (`vt + 0x110`); our slot 68 hook records the bias before the skip.
- **Slot 81** `_setPolygonMode` (`0x821C5228`): state 52, the mode mapped in place. **Device only.**
- **Slot 84** `setVertexDeclaration` (`0x821CE5A0`): two calls. `0x821CE5E0`
  (`D3D9VertexDeclaration::getD3DVertexDeclaration`) looks the device up in the declaration's map
  (`declaration + 24`, @0x821CE60C) and, when missing, builds the Xbox declaration (allocation
  `0x821CD7F8`, creation `0x8276DBC0`) and files it in that map (@0x821CE914). `0x821CE588` stores
  it in the device (`device + 12120`, the dirty mask at `device + 16`). `0x821CE5E0` has no other
  caller, so the map is only filled here; skipped, it stays empty, and the declaration's release and
  destruction walk an empty map. The other callers of `0x821CE588` (`0x821B1000`'s full-screen pass
  and two more) keep setting their own declarations. **Device only**, plus the Xbox declaration
  objects the guest no longer builds: guest heap allocations, not read by game logic.

Conclusion: in the native mode all eleven slots can skip the guest implementation, with one
condition: slot 37's count-only hook becomes a recording hook. Slots 52, 82, 83, 106 and 124 (separate blending and stencil)
follow the same pattern through `0x821C5480`, but they were not read here. Validation as in step
a: the 20 replays, session recordings with the cvar off and on (the same render state commands),
and a measured run.

## Texture stage slots: evidence, first part (2026-10-10)

- **Slot 41** `_setTextureCoordSet` (`0x821CAD88`, 7 instructions): stores the coordinate set in
  `this + 892 + 24 * unit` (@0x821CADA8; with the byte `this + 736` set, the unit's own index
  instead). **Members only, no device call**: nothing to skip, and the members are read by slot
  50 and `_setTexture`. Keep.
- **Slot 42** `_setTextureCoordCalculation` (`0x821CB0F0`, 7 instructions): stores the
  calculation and the frustum in `this + 896 + 24 * unit` and `+900` (@0x821CB100, @0x821CB104).
  **Members only, no device call.** Keep.
- **Slot 50** `_setTextureMatrix` (`0x821CA930`): copies the matrix, then by the unit's calculation
  (`this + 896 + 24 * unit`: 1, 3, 5, 0) multiplies it with the view matrix's inverse
  (`0x821BE948` on `this + 2196`), constant matrices and, for projective texturing (5), the unit's
  frustum's matrices (virtual getters at its vtable `+332`, `+336`, `+340`). It transposes the
  result (`0x824639B8`) and compares it with the identity (vector compares @0x821CACC0..@0x821CACDC).
  After that it computes an address and returns: **no store outside its stack frame and no device
  call** (a Runic difference: OGRE 1.7's D3D9 sets the texture transform and the stage's
  `TEXTURETRANSFORMFLAGS`, and the Xbox D3D has no fixed-function texture transform). The
  matrix products (`0x821C2CC8`, `0x821BE948`) write only into its stack frame. The frustum getters
  update the frustum's own lazily computed matrices, which any later reader recomputes the same
  way. **Nothing to keep: skippable**, about 305 calls per frame, each with up to six matrix
  products. Our slot 50 hook records the matrix from its arguments before the skip.
- Slots 39, 40, 33 and 34 (`_setTexture`, `_setVertexTexture`, `_disableTextureUnit`,
  `_disableTextureUnitsFrom`) write the render system's stage members and call the device's
  `SetTexture`: class B, still to be read.
