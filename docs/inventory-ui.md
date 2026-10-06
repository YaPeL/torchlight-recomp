# Select character/inventory UI: first focused capture

Run: 2026-09-13 local / 2026-09-14 UTC. NVIDIA RelWithDebInfo application,
matching SDK Release Xenos plugin. One reproduction, no renderer behavior fix.

## Reproduction

The user reports that the initial Select page displays only the 3D character;
panels, slots, icons and text are invisible. The next inventory page works.
For this capture the user confirmed switching to the working page after roughly
two seconds and that it rendered correctly. The exact page-switch edge was not
recorded; the later samples cannot be individually labeled from pixels alone.
The process exited normally through the user's interaction.

Runtime `run-1/runtime.log:31` confirms the selected physical device is NVIDIA
GeForce GTX 1050 Ti with Max-Q Design. The executable, SDK runtime and plugin
hashes are in `docs/bringup-artifacts/inventory-select/capture-build-hashes.json`.
The runtime is unchanged. The capture build preserves the current stencil,
SHADER_READ | SHADER_WRITE compute visibility, presenter and POSIX wait fixes.
No GPU error/device-loss/VUID strings were recorded; this was not a validation-layer test.

## Opt-in mechanism

`tools/inventory_capture.cpp` is an LD_PRELOAD observer enabled by
`TORCHLIGHT_INVENTORY_CAPTURE=<existing-output-directory>` and
`TORCHLIGHT_INVENTORY_DEVICE=/dev/input/event10`. It opens an independent read-only
evdev descriptor, verifies BTN_SELECT support, requests the monotonic event clock,
and triggers once on EV_KEY/code314/value1. It does not grab the device, inject
input, or observe Start as a trigger. The game receives input normally.

`patches/rexglue-inventory-observation.patch` adds weak callbacks around the
unchanged IssueDraw body and after Xenos queue submission. Without the preload,
the callbacks are absent. No generated code or input behavior changed.

The press marker records evdev and receipt timestamps, the existing host-present
and guest-submission counters, and the latest observed Xenos frame/submission.
These are a correlated snapshot, not a claim that input was consumed by that
specific GPU submission. Counter addresses are RVAs resolved from this exact
executable's symbols; they must be resolved again for a different executable.

The observer retains up to three preceding Xenos frames, the first post-trigger
frames, and roughly half-second samples afterward, nominally for 5.5 seconds.
The 128 MiB payload cap stopped this run at about +3.1 seconds, during frame897.
It retained 13 frames and 47 complete submission streams. Frame897 is partial;
absence of later commands in that frame must not be interpreted as dropped work.
There is no continuous whole-run disk dump. Retention/copying adds diagnostic
CPU/memory overhead and this run is not a performance measurement.

Each draw records result, shader hashes, pipeline description, viewport/scissor,
depth/stencil and blend/write-mask state, and raw guest state/constant/fetch
register ranges 0x2000..0x23ff and 0x4000..0x5002. Entry/exit stream offsets relate
the request to actual host commands, including successful early returns.
Each retained submit records command buffer/fence/index/result, target image/view
identities, and the entire deferred stream. The stream includes actual draws,
descriptor-set binds, barriers/layouts, clears, copies, dispatches and attachment
switches. Cached pipeline descriptions may be stale on early-return/copy paths;
use actual stream binds when attributing a draw.

Decode/reproduce the offline analysis with:

```sh
gdb -q -batch docs/bringup-artifacts/present-semaphore-fix/types.o -ex 'source tools/inventory_stream_schema.py'
python3 tools/decode_inventory_stream.py docs/bringup-artifacts/inventory-select/run-1
python3 tools/analyze_inventory_capture.py docs/bringup-artifacts/inventory-select/run-1
```

## Concrete observations

Select receipt: monotonic25827447422335ns; evdev25827.447401s;
host present12761, guest counter756, latest Xenos frame756/submission2510.

The first retained frame with the extra inventory work is **759**, whose first
draw starts approximately **67ms after Select**. This identifies onset of the
additional workload, not a proven first incorrect pixel/draw against an oracle.

| Xenos frame | Relative start | IssueDraw calls | Main screen-space UI batch | Matching submitted Vulkan draws |
| --- | ---: | ---: | ---: | ---: |
| 758 | +0.034s | 365 | 26 | 26 |
| 759 | +0.067s | 460 | 117 | 117 |
| 826 | +1.540s | 563 | 126 | 126 |
| 873 | +2.567s | 526 | 89 | 89 |

The main 2D batch is identified by VS `b66e390297113d20`, PS `7abdfefbcadb4bf9`,
and screen-sized viewport. It is also used by the pre-Select HUD. Without texture
contents or pixel replay, individual commands cannot yet be assigned to specific
missing labels/icons. All observed IssueDraw calls returned true; all 47 captured
vkQueueSubmit results were VK_SUCCESS. True alone is insufficient, so the table
matches actual Vulkan draws and pipeline binds, excluding transfer helper draws.

For frame826, draw indices436..561, submission2793:

- All 126 UI requests emit their actual Vulkan draw; none of that batch is dropped.
- Viewport and scissor are 0,0,1280,720. Rasterizer discard is disabled.
- Depth comparison is ALWAYS, stencil test disabled, RGBA write mask0xf.
- Color/alpha blend factors are SRC_ALPHA and ONE_MINUS_SRC_ALPHA, ADD.
- Actual guest graphics pipeline is `0x7fff50001780`
  (decimal140734535571328 in the raw JSON).
- All draw to color view `0x7fff70aff160`, image140735083963056, RT key32768.
  The attachment layout in the draw stream is COLOR_ATTACHMENT_OPTIMAL.
  A later snapshot showing layout5 is after subsequent transitions, not evidence
  of drawing in the wrong layout.
- Frame873's 89 UI draws use the same actual pipeline, color view, full-screen
  viewport/scissor, write mask and depth/stencil/blend description.

The small character viewport at232,240,249,386 uses different character shaders
and GREATER_OR_EQUAL depth comparison. The following UI batch restores the
full-screen viewport/scissor. Captured state therefore does not support a simple
failure to restore the character viewport or a lingering stencil/depth rejection
as the explanation for that batch's invisible pixels.

Some other successful IssueDraw calls emit no VkDraw: copy-mode calls emit compute
dispatches/resolves, while a few color-depth calls return with an empty interval.
Those calls also occur before opening inventory. Their precise early-return
reason was not tagged, so they are not all classified as expected behavior.
No missing-UI attribution is established for them.

## Conclusion and stop point

**Root cause remains unproven; no UI fix made.** The capture excludes wholesale
absence/drop of the identified inventory 2D batch, empty viewport/scissor,
disabled color writes, and depth/stencil rejection for that batch. It does not
exclude missing additional guest draws, incorrect geometry/UV/alpha, texture or
descriptor contents, shader output, or later resolve/composition corruption.
Matching shader and pipeline IDs do not prove that resources or outputs match.

The smallest next useful capture is **one replayable frame from each page**, still
keyed to Select: inspect a known missing quad in this identified 2D batch, its
post-VS positions/UVs, sampled texture/alpha and color attachment before/after the
draw and final resolve. This can separate invisible shader/texture output from
correct UI pixels subsequently lost during composition. Reuse the installed
RenderDoc tooling if practical; avoid another broad register dump. The existing
capture has descriptor-set handles and fetch constants but not descriptor image
contents, vertex payloads, or pixel history. No Xbox/Xenia pixel/draw oracle was
captured. Stop after this first focused capture as requested.

All evidence is under `docs/bringup-artifacts/inventory-select/run-1`, including
`analysis.json`, raw and decoded streams, frame/register snapshots, runtime log,
GDB log, and preserved observer-console output. No Intel test, speculative
renderer change, or unrelated investigation was performed.

## Pixel replay result — 2026-09-14

**First proven divergence: the broken page's fragment shader discards a quad
that writes opaque UI pixels on the working page. Its pixels are already absent
immediately after the draw, before final resolve/composition. No fix applied.**

Artifacts: `docs/bringup-artifacts/inventory-pixels/`.

### Captures and isolation

Two replayable RenderDoc1.46 captures were saved in the same NVIDIA/X11 run:
`broken_capture.rdc` and `working_capture.rdc`, approximately109MiB each.
The user confirmed the first page still displayed only the character, and that
the next page displayed the pet and its UI correctly. The exported final images
independently show this contrast. The process exited normally.

`tools/inventory_renderdoc.cpp` is a small preload using the already-present
weak swap/draw observation hooks. It opens a separate read-only evdev descriptor
and marks only BTN_SELECT314/value1. After one second of settling, it starts
capture on the first IssueDraw following a guest swap and ends on the first draw
after the next swap. This includes the preceding frame's IssueSwap/EndSubmission;
it does not capture merely one of the much more frequent host presents.
Broken capture guest-swap sequence1053→1054, working3955→3956; both returned1.
The second capture was armed with `capture-working` only after manual confirmation
that the working page was visible. No register dump or input injection was used.

The first attempt loaded RenderDoc's API without its Vulkan layer and saved no
capture; preserved under `attempt-no-layer/`. Corrected launch settings explicitly
enabled `VK_LAYER_RENDERDOC_Capture`. GDB used `startup-with-shell off` to preserve
the preload/SDK environment. No Vulkan validation layer was added. The historical
X11 capture-only instance compatibility gate was temporarily reused, then its
source and the exact pre-capture installed runtime/plugin were restored. All four
byte-for-byte restoration checks pass in `restoration-checks.json`.

`tools/replay_inventory.py` drives the installed portable qrenderdoc with an
artifact `request.json`. The initial all-draw replay scan was stopped because it
was slow; targeted replay of the specific events below succeeded. This did not
require another game run or another capture. No device-loss investigation,
renderer fix, or Intel test was performed.

### Exact matching quad

| Property | Broken page | Working pet page |
| --- | --- | --- |
| Capture | broken_capture.rdc | working_capture.rdc |
| RenderDoc event | **6109** | **6408** |
| Draw ordinal (all Vulkan draws, one-based) | 695 | 708 |
| Command | vkCmdDraw, 6 vertices | vkCmdDraw, 6 vertices |
| Pipeline / VS / PS | ResourceId::615 /553 /522 | Same |
| Color attachment | ResourceId::3793 | Same |
| Texture / image view | ResourceId::3851 /3859 | Same |
| Texture descriptor set | ResourceId::13166 | ResourceId::35392 (same image/view contents) |
| Sampler | ResourceId::3850 | Same |

This is the first guest screen-space quad after the final UI transfer helpers,
sampling a bordered square from the UI atlas. Its bounds are **x870..915,
y616..661** in the1280x720 guest image. It is a matched draw by geometry,
shader, UV and texture content; a particular game widget/name is not inferred.
It becomes visible immediately after the working draw. Later UI can cover it;
the comparison does not claim this quad stays unobscured in the final pet page.

Post-VS data are byte-identical for all six vertices (48-byte stride):

- clip-space x0.35937494..0.42968744, y0.71111113..0.83611113;
- z0.0009950099047, w1;
- UV u0.0009765625..0.0654296875, v0.2548828125..0.3203125;
- interpolated vertex RGBA=(1,1,1,1) at every vertex.

Both sample the same1024x1024 BC2/DXT3 UNORM atlas, base mip0, one mip, identity
RGBA swizzle. The exported RGBA texture PNGs are byte-identical (SHA256
e5804d094288dcfa4448d6ecc2e5a02b53146f111e1018c40de0211aef8f2b00).
The referenced center texel around(34,294) is opaque RGBA=(8,4,0,255).
Both have linear min/mag/mip filtering, normalized UVs, wrapU/V, clamp-to-edgeW,
LOD0..1000 and bias0. The optional signed-texture descriptor is unbound in both;
this is not a page-specific descriptor difference. The complete descriptor and
constant-buffer identities are retained in `bindings-*-ShaderStage.*.json`.

Vertex and fragment shader disassemblies also hash identically between the two
events; hashes are recorded in `quad-comparison.json`. Matching executable shader
code does not establish that the bound system constants match.

### Pixel evidence and A-versus-B answer

At guest pixel **(892,638)**, exported immediately before/after the corresponding
draw, with no subsequent guest draw executed:

| Result | Before draw | After draw |
| --- | --- | --- |
| Broken event6109 | (23,31,40,255) | **(23,31,40,255)** |
| Working event6408 | (23,31,40,255) | **(8,4,0,255)** |

The entire broken attachment is unchanged by this draw (RGB and alpha checked).
The working draw changes the expected45x45 rectangle. The pre/post images are
`broken-quad/event-6108-target-0.png`, `event-6109-target-0.png`, and corresponding
`working-quad/event-6407-target-0.png`, `event-6408-target-0.png`.

RenderDoc pixel history at that point identifies event6109/primitive1 with:

```
shaderDiscarded = True
depthTestFailed = stencilTestFailed = scissorClipped = False
backfaceCulled = depthClipped = viewClipped = False
sampleMasked = predicationSkipped = unboundPS = False
preMod == postMod
```

See `broken-history/history-3793-892-638.json`. Its zero `shaderOut` values are
discarded-fragment placeholders; they are **not evidence that the shader wrote
transparent black**. No committed color output exists for that fragment.
Three later UI events6181/6236/6239 overlapping the same point are likewise
reported shader-discarded. The final guest color image after6497 and presented
image at6537 both retain the missing UI, corroborating the live symptom.

**Answer: A, specifically shader discard before a color write.** B (correct
pixels from this quad subsequently lost in final composition) is ruled out for
the inspected quad. This is narrower than claiming every missing widget has
the same cause.

### Stop point / targeted fix status

Stop at this first proven pixel divergence as requested. No renderer patch.
The fragment disassembly contains discard paths for the emulated alpha test and
alpha-to-coverage; the exact executed discard predicate and its source constant
were not determined in this pass. It would be speculative to disable either.

If continued, the smallest next investigation is to compare the **bound
XeSystemConstants for these two exact events** and debug the discarded pixel
through the relevant Kill branch. Establish whether the mismatch originates in
guest alpha state, constant upload/binding, or translation before proposing a
targeted fix. The saved captures already contain the needed replay evidence;
another broad capture is unnecessary.

## Discard predicate proven and translator fixed — 2026-09-14

**Root cause: SPIR-V texture fetch translation applied the color/alpha exponent
from fetch-constant word4 (LOD state), instead of word3 (result exponent state).
The character page's legitimate LOD bias of−1 was misread as a color exponent
of−16. Opaque alpha became1/65536 and correctly failed the game's alpha test.**
This is origin **D: translation**, not evidence of a bad guest alpha-test value
or stale system-constant binding. No game launch or additional capture was used.

### Bound values and complete discard dataflow

Raw/decoded bound constants are retained in `inventory-pixels/broken-discard/`
and `working-discard/`. Each dump reads the buffer resource, offset and range
from the actual event's descriptors. `discard-cause.json` contains the numerical
comparison.

| Field | Broken6109 | Working6408 | Meaning |
| --- | --- | --- | --- |
| XeSystemConstants.flags, byte0 | 0x00048c00 | Same | Alpha pass function bits16:18 =4 (GREATER) |
| alpha_test_reference, byte272 | 0x3ca0a0a1 /0.019607843831181526 | Same | 5/255 |
| alpha_to_mask, byte276 | 0 | Same | Alpha-to-coverage path disabled |
| flags MSAA bits13:14 | 0 | Same | One sample |
| texture_swizzled_signs[0], byte176 | 0 | Same | Unsigned RGBA sampling |
| Fetch0 word2 | 0x007fe3ff | Same | 1024×1024 dimensions used for fetch-coordinate adjustment |
| Fetch0 **word3**, byte12 | **0x00a80d10** | Same | Signed bits13:18 =0, the correct result exponent |
| Fetch0 **word4**, byte16 | **0x003e0003** | **0x00000003** | Signed bits12:21 /32 =LOD bias−1 versus0 |
| Word4 bits13:18, incorrectly used by shader | **−16** | **0** | Incorrect color/alpha multiplier2^-16 versus1 |

The512-byte SystemConstants blocks differ only at byte4:
vertex_index_load_address0x1cd90000 versus0x1cb13000. The fragment shader does
not use that address. All alpha, sign, swizzle and sample-related fields match.
Boolean/loop constants are identical. Fetch-block differences at byte744 and
later are unrelated vertex-address state; this fragment's texture calculation
accesses only fetch0's word2 and word4 in the original shader.

The fragment has exactly two Kill branches:

1. **Alpha-test rejection**, disassembly line374 in the original shader:
   `_320 = (flags >>16) &7`, `_353 = alpha passes selected comparison`,
   `_354 = !_353`; `if (_354) Kill()`. With function4 this is precisely
   **`if (!(alpha > 0.019607843831181526)) discard`**.
2. Alpha-to-coverage empty coverage, line449: if `alpha_to_mask !=0`, derive
   sample thresholds from its low8 dither bits, pixel x/y parity, MSAA bits13:14,
   and output alpha; discard when computed coverage mask is zero. Both events
   have `alpha_to_mask=0`, so this entire branch is unreachable. Pixel(892,638)
   has even/even parity; it cannot account for the difference.

Relevant alpha dataflow at pixel(892,638), reconstructed from the exact SPIR-V
and bound values:

```
unsigned texture sample alpha = 1
vertex/interpolator alpha = 1
LOD gradient multiplier = exp2(signed(word4[12:21]) /32)
                       = 0.5 broken, 1 working
// The atlas has only one mip; both sample the same opaque region.
BAD result exponent = signed(word4[13:18]) = -16 broken, 0 working
alpha = sampled_alpha * ldexp(1, BAD exponent) * vertex_alpha
      = 0.0000152587890625 broken, 1 working
pass = alpha > 0.019607843831181526
     = false broken, true working
discard = !pass
```

SPIR-V IDs tie this to the captured executable shader: `_151` loads
`fetch_constants[1].x` (word4); `_295` extracts signed bits13:18 from `_152`;
`_296=ldexp(1,_295)` scales `_294` sampled alpha into `_300`. The shader writes
the multiplied interpolator result, then `_328` reads output alpha and the
`_354` branch kills the fragment. The expected exponent comes from
`fetch_constants[0].w` (word3), which has zero exponent in both captures.

RenderDoc's interactive SPIR-V debugger declined both shaders with
`Unsupported capability 'RoundingModeRTE'`, returning no debugger/steps. No
invented single-step trace is claimed: the exact arithmetic above is a dataflow
trace corroborated by native GPU pixel history and the single-operand replay
experiment below. Shader floating-point capabilities were not removed.

### Why this is translation, and the smallest fix

`xenos.h` defines `exp_adjust` as signed6 bits at word3/bit13 and `lod_bias` as
signed10 bits at word4/bit12. The SDK D3D translator independently reads
`RequestTextureFetchConstantWord(tfetch_index, 3)` for result exponent. The
Vulkan translator instead explicitly loaded word4 for both operations.
The Vulkan command processor uploads the fetch block with a direct memcpy
from `SHADER_CONSTANT_FETCH_00_0`; no upload conversion explains or requires
reading the next word as a result exponent. The bound word3 already contains
the correct zero exponent. Fixing only shader interpretation suffices.

Changed only `src/graphics/pipeline/shader/spirv_translator_fetch.cpp` in the
SDK: load word3 using the existing packed fetch-block indexing and pass its
signed value to the result-exponent bit extract. Word4 remains the source of
LOD bias. Durable patch:
`patches/rexglue-vulkan-texture-exponent-word.patch`.

No alpha-test/coverage/discard change, guest change, constant-upload change,
stencil change, timing change or renderer refactor. The native code build
passed for SDK Release rexruntime/rexgpu-xenos; matching binaries were installed
and the application-local Xenos plugin updated. Hashes and build log:
`exponent-fix-hashes.json`, `exponent-fix-build.log`. The executable itself was
not rebuilt or changed. On-disk shader storage keeps guest microcode and the
pipeline loader retranslates it, so no shader-cache deletion/version bump was
needed for this code-only translation correction.

### Replay validation and its boundary

Before editing the SDK, `tools/inventory_exponent_replay_patch.py` constructed a
replay-only SPIR-V variant adding a word3 load and replacing **only the exponent
extract's source operand**. It preserves word4 LOD calculation, all Kill
instructions, and floating-point capabilities. Original RDC files are untouched.
The production source fix implements that same change for any fetch index.

Native NVIDIA replay of that variant, broken event6109:

- `shaderDiscarded=False`;
- fragment output `(0.0313725508749485,0.01568627543747425,0,1)`;
- post-draw pixel `(8,4,0,255)`, matching the working draw;
- after the UI batch at6497, the character page's panels, slots, text, icons
  and character are visible in the guest color attachment.

Working event6408 with the same correction still passes and produces the same
opaque output. Its entire post-draw RGBA image is byte-identical to its original
replay image. This is a captured-frame correctness comparison, not an Intel or
performance test.

See `broken-exponent-corrected/event-6497-target-0.png` for the restored guest UI,
and corrected pixel histories in `broken-exponent-corrected/` and
`working-exponent-corrected/`. The final host-present image in the modified
replay still shows the originally missing UI; shader replacement's result in
the guest target must not be represented as validation of the captured
host-present chain. No new presentation root cause is inferred from that replay
limitation. **Live output from the newly built plugin has not been tested**, in
accordance with the existing-captures-only constraint. Therefore the confirmed
fix result is restoration of this UI's guest rendering in corrected-shader
replay plus a successful native SDK build, not a claimed fresh gameplay test.
