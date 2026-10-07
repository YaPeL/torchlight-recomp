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
| -- a block before the scene, mostly arithmetic (`0x821C7158`, called from `0x821A6070`) | 11.6 % |
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

## Upstream report (ReXGlue codegen)

For the agent maintaining the ReXGlue patches: in the generated code every PPC register is a field
of `PPCContext` that is stored on every write and reloaded after every call (the context is passed
to every callee), and a compare writes all four bytes of its CR field (`PPCCRRegister::compare`)
whether or not they are read. Measured on Torchlight's generated code: 903 x86 instructions for a
176-instruction PPC function, 238 of them context accesses. Possible directions: keep registers in
locals within a function and write back only the live ones at calls and returns; set the CR bits
lazily or only the ones a later branch reads; and compile the generated code with the default code
model on Linux when the text fits.
