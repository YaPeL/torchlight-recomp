# Where the guest's main thread goes in the slow scenes (2026-10-07)

Profile of the fight with the NPC on the fixed-floor saved game (`docs/performance-profile.md`),
native mode with `--native_skip_guest_d3d=true`, release build, `perf record --call-graph
dwarf,16384 -F 499` (full stacks: the LBR call graphs of the earlier profiles stop at 32 branches
and undercounted every deep subtree). 18,860 samples of the game's main thread over the 38 s of the
fight. Function names come from the call structure and the OGRE 1.7 sources; the ones not yet
confirmed in the recompiled code are marked *hypothesis*.

## The frame

| Subtree | Main thread |
|---|---|
| Guest main loop (`0x821F8818`) | 90.5 % |
| - frame rendering (`0x821EE0D0`, `_updateAllRenderTargets` at `0x821EE1C0`, slot 101) | 72.4 % |
| -- the main scene: `SceneManager::_renderScene` (`0x821BC5A0`, *hypothesis*) | 51.0 % |
| --- `renderSingleObject` (`0x821C5FA0`, per the legacy) | 32.5 % |
| --- `_setPass` (`0x821C5730`, per the legacy) | 11.0 % |
| -- culling: Runic's octree walk (`0x821C7158`, from `_findVisibleObjects` `0x821A6070`; see below) | 11.6 % |
| -- another render target's update (`0x82198608`; shadows or a render texture, *hypothesis*) | 19.9 % |
| - game logic, outside rendering (`0x821F9DA8`) | 17.9 % |

By where the time is spent (leaf functions):

| Class | Main thread |
|---|---|
| Recompiled guest code | 76.4 % |
| This project's producer (RenderSystem hooks, with the libc/std it calls) | 19.5 % |
| Other host code (kernel exports, libm, the SDK) | 4.1 % |

`renderSingleObject`'s 32.5 % is 18.1 points of guest code and 13.4 of the producer: the earlier
"at least 39 % in the guest's per-object rendering" mixed both.

## The guest's own time, by kind of function

76.3 % of the main thread, very flat: no function reaches 1.7 %, the top 45 add up to 32.6 %.
Classified by instruction mix (from `generated/default`):

| Kind | Main thread | Largest |
|---|---|---|
| Control logic | 37.1 % | `0x821C8C00` 1.36 %, `0x821C7158` 1.30 % |
| Arithmetic (40 % or more floating point or vector instructions) | 14.5 % | `0x821C9100` 1.65 % (slot 43, blend colours), `0x821C2CC8` 1.57 % (93 % floating point, no branches; called from `0x821C30B0` and `0x821C2C38` under `renderSingleObject`: a matrix product, *hypothesis*), `0x821C74A0` 1.07 % (vector), `0x821C1768` 0.57 %, `0x821C0E78` 0.55 %, `0x821C47F0` 0.54 % |
| Dispatch (virtual calls) | 8.5 % | `renderSingleObject` 1.61 %, `_setPass` 1.29 %, `0x82201EB0` 0.89 % (2,158 instructions, 129 virtual calls: the automatic shader parameter update, *hypothesis*) |
| Mixed arithmetic | 8.3 % | `0x821C8A58` 1.18 %, `0x821C28D8` 0.93 % |
| Tree and map lookups (short loops of loads, no calls) | 7.9 % | `0x82581D98` 1.50 % (the per-device resource map, from `_render` and the bindings), `0x824C6960` 1.02 % (from `0x821C8C00`), `0x8262D6C8` 0.91 % |
| Register save and restore helpers (`__savegprlr_N`, `__restgprlr_N`) | 4.6 % | |

## How the recompiled code comes out

- **The PPC registers live in memory.** Every result is stored back into the context (`ctx`),
  every compare writes the four bytes of its CR field (lt, gt, eq, so) even when one is read, and
  every call passes the context, so nothing stays in host registers across it. The 176 PPC
  instructions of `0x821C9100` become 903 x86 instructions, 238 of them on the context.
- **`volatile` guest memory accesses are not the cause.** The same generated file compiled without
  `volatile` in `REX_LOAD_*`/`REX_STORE_*` (offline, nothing changed in the repository): 55,232
  against 55,241 instructions.
- **The large code model.** The SDK compiles every Linux x86-64 target with `-mcmodel=large`
  ("large executable support", `rexglue_apply_target_settings`): every call between guest
  functions is a `movabs` and an indirect call. The same generated file with the default code
  model has 5.6 % fewer instructions (52,811 against 55,955) and direct calls (2,054 of 2,114). The
  executable's text is 67 MB, far from the 2 GB the small model allows; the Windows build already
  uses the default.
- **`fmadd` is a call** to libc's `fma` (`__fma_fma3`, 0.75 % of the main thread): the build
  targets x86-64-v2, which has no FMA instruction. x86-64-v3 inlines it but measured no clear gain
  (`docs/performance-profile.md`).
- **Byte swaps** on every load and store come with a big-endian guest; the physical address offset
  is 0 on Linux and one compare per access on Windows and macOS arm64.

## Candidates, by gain and risk

| # | Candidate | Estimate (main thread) | Risk | Where |
|---|---|---|---|---|
| 1 | The default code model on Linux (no `-mcmodel=large`) | measured: +11 % main menu, +4.5 % dungeon still, fight within noise (`docs/performance-profile.md`); done | Low (build flag; Windows already) | This project's CMake; report upstream |
| 2 | Native implementations of the hottest lookups (`0x82581D98`, `0x824C6960`, `0x8262D6C8`) | about 2 % | Low to medium (read only; the comparison must match) | Here, as the memcpy |
| 3 | Native register save and restore helpers | 1-2 % | Low (fixed semantics) | Here, or in the codegen upstream |
| 4 | Native implementations of the five hottest arithmetic functions | about 3 % | Medium (results must be bit identical: fused multiply-add, vector denormals; some may feed game logic) | Here, with the evidence of each |
| 5 | Automatic shader parameters, light lists (`0x82201EB0`, `0x821C26D0`) | | High (reimplementing OGRE 1.7's automatic parameters) | Not recommended |
| 6 | Codegen: keep guest registers in host registers between calls, and set only the CR bits that are read | possibly the largest | | Upstream report (below) |

Native implementations follow the guest copy hooks (`hooks/guest_copy.h`): a pure function
identified by behaviour, evidence in `guest_abi/`, a fallback to the guest's code, a cvar to turn
it off, and synthetic tests.

## Culling: Runic's octree walk

Every scene manager the game creates (`0x82209F88`: `SMBKInstance`, `SMInstance`, `SMUIInstance`,
`SMRBInstance`, `SMRBPInstance`, `SMAMInstance`) is an `OctreeSceneManager`: Runic's
`createSceneManager` (`0x824B03F8`) takes the first factory whose mask has `ST_INTERIOR`, and the
octree factory registers `0xFFFF` (`0x8251A650`). The PC build does the same (`ST_INTERIOR`
through `Root::createSceneManager`, with the stock plugin).

`_findVisibleObjects` is Runic's (`0x821A6070`, vtable `0x82005B9C` slot 122) and calls an
iterative rewrite of the plugin's recursive `walkOctree` (`0x821C7158`, an explicit stack):

- octants: the loose bounds (box grown by the half size, `0x821C7000`) against the camera
  (`OctreeCamera::getVisibility`, `0x821C0E78`): none, partial or full; a full octant marks its
  children full;
- nodes, in partial octants only: the node's world box (`OctreeNode::_updateBounds`
  `0x821BE630`: the union of its own objects' boxes, no children) against the six planes
  (`Frustum::isVisible`, `0x821C74A0`; the camera's culling frustum when one is set);
- objects of a visible node: no frustum test; `isVisible()` (flags and masks),
  `_notifyCurrentCamera` and `_updateRenderQueue`, as in the stock plugin.

So an object is queued whenever its node's box (the union of the node's objects) touches the
frustum. In the fight and the town square, 25-53 % and 22-31 % of the world draws to the main target
are entirely outside the screen (session replay, coverage with depth test and face culling off);
the PC draws as many or more in the same places.

**Possible bug in the walk, not touched:** the node visibility flag is set once before the walk
(`li r18,1`) and only refreshed in partial octants (`@0x821C72A0..0x821C72BC`); a full octant
reuses whatever the last node test of an earlier partial octant gave (the plugin declares
`bool vis = true` per octant, OgreOctreeSceneManager.cpp:633). A full octant visited right after a
failed node test would then queue none of its nodes. That would leave objects out, never add
any; no visible symptom is known.

### Bucket culling: an improvement over the original

What the walk queues and the screen does not show is OGRE `StaticGeometry`: a region
(`StaticGeometry::Region`, vtable `0x82002D1C`) is one object with one box covering a large piece
of the level, and `Region::_updateRenderQueue` queues every `GeometryBucket` of it (one draw per
material and vertex format, vtable `0x820016B4`) with no test of its own. In the fight 54-58 % of
the queued buckets lie entirely outside the screen, and they are 86-99 % of the draws that do not
reach it (session replay, coverage with face culling, depth test and fragment discard off, joined
with the renderable and owner of each draw logged in the game). In the town square the buckets
are the problem too, but one box per bucket does not catch them: a bucket gathers every piece of
one material in its region (the same building texture on buildings all around the square), and
its box touches the frustum even when every piece is off screen.

`--native_bucket_cull` (native mode only, on by default; `=false` restores the game's behaviour)
gives each bucket its own box, computed once from all the vertices of its vertex data in the
region's space and forgotten in `~GeometryBucket` (`0x8247F728`), brings it to world space at
each test with the region node's current transform, and during the walk of the scene's viewport
does not queue the buckets entirely outside the camera's six planes (`hooks/bucket_cull.h`). Other
cameras (the light map and shadow passes), other scene managers and other renderables are
untouched. With `--native_bucket_cull_pieces` (on by default; `=false` keeps one box per bucket)
a bucket also gets one box per piece, the connected components of its triangles, grouped by
nearness to at most 16 (`kMaxPiecesPerBucket`, which bounds the tests per bucket and frame), and
is dropped when no piece is inside the frustum. The pieces are built on first use, not at level
load: 593 shapes (8776 pieces) in 51 ms in total on entering the town square, within the loading
frames. Every vertex program in our captures places vertices with the RTSS's fixed transform or
its hardware skinning; a vertex program that writes the position any other way turns the culling
off for the session (logged).

Validation: sessions recorded with the culling off but logging what it would drop; on sampled
frames of the fight and the town square, the frame replayed without those draws is identical to
the whole frame at 16:9 (4 frames), 21:9 (6) and 32:9 (6), and every dropped draw is outside the
screen by the coverage probe. Per piece, the same at 16:9 (19 frames), 21:9 (19) and 32:9 (16),
with the dropped draws matched by geometry (vertex buffer and index range), up to 113 per frame. The guest camera's frustum follows the wider frames (other aspect
ratios change the guest's video mode, not our projection). Measurement in
`docs/performance-profile.md`, "Bucket culling".

**Condition for the next release (freeze):** validate it the same way in the other kinds of
dungeon, not only the mine (a session recorded with the culling off and the validation log, and
the frames replayed without the dropped buckets identical to the whole frames). The validation
log and its scripts are local; they log the would-drop decisions by guest swap and draw index.

### The other cameras: light map and shadows (2026-10-09)

Besides the main camera, the town square renders two 512x512 render textures per frame, each from
its own orthographic camera: the light map (`rtt/.../208`) and the shadows (`rtt/.../207`). Measured
per frame (a local diagnostic timing `SceneManager::_renderScene`, `0x821B07F0`, per camera, frame
about 7.2 ms):

| Camera | `_renderScene` per frame | What it queues per walk |
|---|---|---|
| Main (the window) | 3.3-3.5 ms | ~185 objects |
| Light map | 1.0-1.1 ms (walk ~0.35, drawing ~0.65) | ~62 renderables, ~38 of them StaticGeometry buckets |
| Shadows | 0.87-0.91 ms (walk ~0.3, drawing ~0.3, and 0.27 ms of `0x821C8C00`) | ~13 renderables, no buckets |

Bucket and per-piece culling would gain nothing there: counted in the same run (culling off, every
bucket tested against that walk's own frustum), no light map bucket lies outside the light map
camera's frustum, and the shadow camera queues no buckets. Their objects' boxes are +-100000, so a
per-object test drops nothing either. The shadow walk's cost per octree node (~2.4 us) is the main
camera's; most of its ~125 nodes hold objects the walk rejects by their flags (casters only).

`0x821C8C00`, the 0.27 ms, is not a cost of the shadows: it is a once-per-frame update (it compares
the frame number at `this+56` with the current one, then walks a `std::map` at `this+8` and, for
each enabled entry (`+52`), calls three virtuals on the objects at `+8`, `+40` and `+24`), paid by the
frame's first `_renderScene`, which is the shadow camera's. It looks like the scene's animation
update (`_applySceneAnimations`); the classes of the three objects are still to be identified. It
is a candidate for a native reimplementation (3.7 % of the frame).

## Upstream report (ReXGlue codegen)

For the agent maintaining the ReXGlue patches: in the generated code every PPC register is a field
of `PPCContext` that is stored on every write and reloaded after every call (the context is passed
to every callee), and a compare writes all four bytes of its CR field (`PPCCRRegister::compare`)
whether or not they are read. Measured on Torchlight's generated code: 903 x86 instructions for a
176-instruction PPC function, 238 of them context accesses. Possible directions: keep registers in
locals within a function and write back only the live ones at calls and returns; set the CR bits
lazily or only the ones a later branch reads; and compile the generated code with the default code
model on Linux when the text fits.

## Codegen options (2026-10-08)

The ReXGlue SDK at our base (`0c7b01a`) can keep some PPC registers in C++ locals of each
generated function instead of `PPCContext` (manifest keys under `[entrypoint]`). What each one
assumes, read from the SDK's code generator (`src/codegen/builders/context.cpp`,
`control_flow.cpp`, `system.cpp`):

| Option | Moves to locals | Assumption | State |
|---|---|---|---|
| `reserved_as_local` | the `lwarx`/`stwcx.` reservation | both in the same function | on |
| `xer_as_local` | carry and overflow | not live across a call | on |
| `ctr_as_local` | the count register (loops, `bctr`) | not live across a call | on |
| `cr_as_local` | cr0-cr7 | the ABI passes no CR between functions; a function split wrongly by the analysis would lose it at the cut | on |
| `non_volatile_as_local` | r14-r31, f14-f31, v14 and up; drops every call to `__savegprlr_N`/`__restgprlr_N`/`__savefpr_N`/... | no hook reads a caller's preserved registers through `ctx`; a host `longjmp` only restores `ctx` | on (below) |
| `non_argument_as_local` | r0, r2, r11, r12, f0, v32-v63 | nothing passes them between functions | off: the game passes data in `r12` outside the ABI (below) |
| `skip_lr` | stops writing `ctx.lr` at calls and `mflr` | nobody reads LR | off: 15 hook reads of `ctx.lr`, and the game's one `blrl` jumps to `ctx.lr` |
| `skip_msr` | drops `mtmsrd`/`mfmsr` | no critical sections through the MSR | off: the global lock behind them is used at 372 sites |

The four that are on, validated with the game (the 60 tests, a run in the emulated GPU mode, a
10-minute varied session in the native mode with the log and an achievement checked, and the
same saved game loaded and saved again by develop twice and by this build once: the saves differ
no more between the builds than between the two develop runs, only in AI state, positions and
level timers). The replays do not run guest code and only checked that nothing else changed.

Effect: the recompiled code goes from 52.1 to 44.7 MiB (-14 %; `0x821C5FA0` from 3,211 to 2,775
x86 instructions), almost all of it from `cr_as_local` (`reserved` changes nothing, `xer` -2 %,
`ctr` -0.6 %). The frame rate hardly moves (fixed-floor saved game, step overlay, draw skip on,
two runs each):

| | develop | Codegen options |
|---|---|---|
| Fight, FPS | 140.4, 143.5 | 140.6, 145.2 |
| Fight, p99 (1 % low) | 11.68, 10.52 ms (85.6, 95.1) | 9.70, 10.35 ms (103.1, 96.6) |
| Town square, FPS | 110.9, 119.3 | 114.0, 117.5 |
| Town square, p99 (1 % low) | 12.03, 11.70 ms (83.1, 85.5) | 11.92, 11.60 ms (83.9, 86.2) |

About +0.7 % on average, inside the run-to-run spread. The fight's 1 % low is set by the guest's
first-use loads of effects (55-80 ms frames that load textures), which depend on what the monsters
do; in the first codegen run they did not cast, so its 103.1 is not comparable. What the options
remove are mostly stores of CR bytes and counters to the context, which land in the L1 cache and
rarely stall: fewer instructions, not much less time. The larger cost the profile shows, the
register save and restore helpers (4.6 % of the main thread) and the reloads of preserved
registers after every call, is what `non_volatile_as_local` would remove.

### Non-volatile registers in locals (`non_volatile_as_local`)

On since 2026-10-08, on top of the four above. Each generated function keeps r14-r31, f14-f31 and
v14 and up in C++ locals, and the calls to the register save and restore helpers (1,046 call sites)
are dropped: in the profile those helpers were 4.6 % of the main thread, and every call reloaded the
preserved registers from the context afterwards. The recompiled code goes from 44.7 to 41.2 MiB.

What it assumes, and how each point was checked:

- **No hook reads its caller's preserved registers through `ctx`** (they are stale there). Two
  achievement hooks did (`ctx.r28`; `ctx.r29` and `ctx.f30`); they now take the values from
  arguments of the enclosing guest call (the item use `0x822B9A60`, the damage application
  `0x8229C7B0`) and the health setter's own `f1`. The `no_preserved_guest_registers` test
  (`cmake/check_guest_registers.cmake`) fails on any access to r12, r14-r31, f14-f31 or v14 and up
  in `src/`; `CONTRIBUTING.md` states the rule.
- **SEH funclets.** The game's `__try`/`__finally` funclets run on their owner's frame (92
  functions, marked as sharing registers: they keep r14-r31 in `ctx`). The 40 that their owner calls
  directly get the owner's localized r14-r31 copied into `ctx` around the call by the SDK; every
  call site copies all the registers its funclet reads (checked one by one in the generated code),
  and none of the 92 reads a non-volatile FPR or vector register of its owner (that copy only covers
  GPRs). The other 52 are only reachable through exception dispatch, which does not run today
  (`RtlUnwind` is a stub). If dispatch is ever implemented, upstream or in our crash handling, those
  52 need the same r14-r31 copy through `ctx` that the direct calls get (upstream notes D17, D26 in
  `docs/rexglue-upstream.md` on the SDK branch).
- **`setjmp`/`longjmp`** (`0x82864230`, `0x828636E0`, image decoder error paths, never reached) stay
  undeclared, as before: the generated `setjmp` restores only `ctx`, so a declared one would leave
  localized registers indeterminate after a `longjmp` (upstream note D21).

`non_argument_as_local` stays off. The game passes data in r12 outside the ABI: the `__try`/
`__finally` funclets get their owner's frame in it (88 functions start with `r31 = r12 - N`), and
the stack probe `0x82861494` its size. With r12 in a local of both caller and callee, the funclet
sees 0 and a `__finally` does not release the C runtime's heap lock: the game deadlocks about 3 s
after start (under gdb, the main thread and two others wait in `RtlEnterCriticalSection` from the
heap functions `0x82719BB8`, `0x8271EBC0`, `0x82881640`). The same build without that option starts
and plays normally (upstream note D26).

Validation, the same as for the four above plus the two achievements: the 60 tests; a run in the
emulated GPU mode; a varied session of about 15 minutes in the native mode (a new character,
inventory, save and reload, dungeon fights, a floor change, fishing, town); PC achievements with the
observation trace and the developer command `HURTME 40` (the potion given to the pet and the
maximum damage count as on develop); and the fixed-floor save loaded and saved again: it differs
from develop's no more than develop's own runs differ from each other (23 bytes where three develop
runs agree and this build does not, all in AI state, positions and timers). The replays do not run
guest code and only checked that nothing else changed.

Measurement against develop with the four options above (fixed-floor saved game, step overlay, draw
skip on, two runs each; "lightning" marks the runs where the bubble monsters cast theirs, whose
first-use loads set the fight's worst frames):

| | develop | non_volatile_as_local |
|---|---|---|
| Standing still, FPS | 191.0, 191.5 | 205.1, 214.2 |
| Fight, FPS | 146.6 (lightning), 158.0 (none) | 159.9 (lightning), 157.7 (lightning) |
| Fight, p99 (1 % low) | 11.04, 9.79 ms (90.6, 102.1) | 9.87, 10.88 ms (101.3, 91.9) |
| Town square, FPS | 123.3, 117.3 | 123.0, 131.3 |
| Town square, p99 (1 % low) | 11.03, 11.32 ms (90.7, 88.3) | 12.46, 11.18 ms (80.3, 89.4) |

About +10 % standing still, +9 % in the fight (the pair with the lightning in both runs) and +6 %
in the town square on average; the town square's 1 % low stays inside its run-to-run spread.
