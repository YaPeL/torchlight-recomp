# Vulkan device-loss investigation — 2026-09-13

The first confirmed invalid Vulkan operation is **premature reuse of a binary
presentation semaphore in VulkanPresenter**. A fresh NVIDIA/Debug capture stops
at `VUID-vkQueueSubmit-pSignalSemaphores-00067`, before guest GPU submissions.
The semaphore is selected by rotating paint-submission slot, while safe reuse
depends on completion of the presentation that consumed that semaphore.

This establishes the cause of the validation failure and is the first concrete
device-loss candidate. **It does not yet establish that this violation caused
the previously captured VK_ERROR_DEVICE_LOST aborts.** Investigation stopped
at this fault, before changing the renderer or deliberately continuing through
invalid submissions to another abort. The subsequently authorized fix is implemented
and undergoing validation below.

## Current result after the authorized fix

The per-image present-semaphore and explicit presentation-fence retirement fix
is implemented. **Device loss still reproduces without any preceding Vulkan
validation error.** Three NVIDIA validation launches report no semaphore reuse
or lifetime VUIDs; the optimized launch nevertheless hits `VK_ERROR_DEVICE_LOST`
in Xenos command submission. The fix addresses the confirmed lifetime violation,
but must not be presented as a fix for historical device loss. No further renderer
changes were made after this capture. Intel validation is waived by user request.

## Capture

The existing Debug executable ran with the NVIDIA ICD, installed wait-fixed
runtime, Xenos plugin, extracted game root and a private copy of the pond save.
Khronos validation was loaded; core checks remained enabled and synchronization
validation was requested through `vk_layer_settings.txt`. No RenderDoc, minimap
diagnostics, pacing changes, GPU optimization or generated-code changes were used.

SDK Release libraries lack DWARF types/locals. A separate type-only object was
compiled from matching SDK headers and build definitions, then loaded into GDB's
symbol tables only. It was never linked into or loaded by Torchlight. Function
entry breakpoints captured original object pointers and Vulkan arguments; a
bounded 96-call history recorded submit/present packets, fence statuses,
command-pool resets and command-buffer end results. These debugger pauses make
this a correctness capture, not a performance sample.

There was an initial debugger-setup stop on an informational loader callback
because type information was missing. It is retained in the debugger log but
is not a Vulkan validation failure. After loading types and correcting the
capture helper, execution resumed until the first error. The user-reported
black window corresponds to these debugger stops during startup: no guest GPU
submission had happened at the validation stop. It is not evidence of another
uncontrolled black-startup hang. The diagnostic inferior was explicitly killed
after preserving evidence; no normal-exit or new device-loss claim is made.

## Concrete state at the error

| Item | Captured value |
|---|---|
| Queue | `0x55555d679740`, graphics and present family 0 |
| Swapchain | `0x2570000000257`, 3 images, 1920×1080 host extent |
| Paint submission / slot | 130 / 1 (`130 % 3`) |
| Completed paint submission | 129; no pending paint fences |
| Acquired paint fence | `0xe100000000e1` |
| UI tracker | current 129, completed 128, acquired fence `0x2660000000266` |
| Acquire semaphore | `0xf000000000f` |
| Signal / present-wait semaphore | `0x100000000010` |
| Command pool | `0x110000000011` |
| Draw command buffer | `0x55555dbdbcb0` |
| Acquire wait stage | `0x400`, COLOR_ATTACHMENT_OUTPUT |
| Previous image using this present semaphore | 2 |
| Newly acquired image | 0 |

The recorded sequence immediately preceding the error is:

1. Draw submit signals semaphore `0x100000000010`; returns VK_SUCCESS.
2. Present image 2 waiting on that semaphore; returns VK_SUBOPTIMAL_KHR.
3. Draw/present image 0 using the next slot's semaphore.
4. Draw/present image 1 using the next slot's semaphore.
5. The paint fence reports VK_SUCCESS. Slot 1's command pool resets successfully
   and its command buffer ends successfully.
6. The next draw submit attempts to signal `0x100000000010` again, this time
   for acquired image 0. Validation stops it: image 2 has not been reacquired,
   so completion of the prior presentation wait has not been established.

The packet at step 6 contains one command buffer, one acquire-semaphore wait and
one signal semaphore, with the handles above. It has **no return result yet**:
the debugger stopped inside validation before this vkQueueSubmit returned.
Earlier submits returned success; suboptimal presentation is not device loss.
The first fault is the semaphore's reuse condition, not a recorded failed
vkEndCommandBuffer or an observed reset of a pending command buffer.

Native stack: VulkanInstance debug callback → validation-layer submit checks →
VulkanPresenter::PaintAndPresentImpl → Presenter::PaintFromUIThread →
Window::OnPaint → SDL event loop. The callback was allowed to finish logging,
then execution remained stopped inside validation.

Xenos state independently supports the early host-presentation location:
`device_lost_=false`, `submission_open_=false`, `submission_completed_=0`,
`frame_current_=1`, no in-flight fences or wait semaphores, empty submitted and
writable command-buffer collections. The submitted deque's raw start/finish
pointers match (its type-only GDB pretty-printer could not render its elements).
The GPU Commands thread is waiting in the command-processor worker; the main
guest thread is in startup code rooted at `sub_821C0298`, not the earlier
post-device-loss `sub_821A5C10` progress spin.

## Why the current reuse check is insufficient

`src/ui/vulkan/vulkan_presenter.cpp:1536` waits for an older **draw submission**
and chooses a slot modulo the three-slot ring. At line 2179 it takes that
slot's `present_semaphore_`, signals it in the submit at line 2204, then supplies
it to vkQueuePresentKHR. Waiting on the draw fence only establishes completion
of the draw submission, not consumption of the semaphore by the presentation
engine. The same queue handle does not repair this lifetime assumption.

Khronos describes this exact VUID and recommends selecting a present-wait
semaphore by acquired swapchain image index, with the acquire wait establishing
safe reuse. See the [Vulkan Guide's swapchain semaphore reuse explanation](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html).

The same VUID appeared in the earlier minimap validation log, which also recorded
`VUID-vkDestroySemaphore-semaphore-05149` during teardown. Neither was introduced
by this capture or by the audio wait fix. The fresh captured violation occurs
within one swapchain, so swapchain recreation is not required to demonstrate it.

## Original narrow fix proposal

1. Give each swapchain image its own present-wait semaphore in PaintContext,
   allocated using the actual swapchain image count.
2. After vkAcquireNextImageKHR returns `swapchain_image_index`, select that
   image's semaphore for both vkQueueSubmit's signal and vkQueuePresentKHR's wait.
   Keep acquire semaphores, command buffers and draw fences in the existing
   per-submission slots.
3. Keep each semaphore set with its swapchain generation during retirement.
   Do not immediately destroy/recycle it using only the existing draw-fence
   completion check. Explicit presentation fences via supported
   VK_KHR/EXT_swapchain_maintenance1 are the direct mechanism for retirement;
   extension support is not yet established here. The current retirement path
   has the same missing presentation-completion proof, so ownership/destruction
   must be reviewed along with the small steady-state indexing change.

This targets presentation synchronization and resource ownership. It requires
no shader, render-target, guest command processor, stencil-transfer, wait-loop,
frame-pacing or controller behavior change. Before claiming device-loss repair,
validate absence of reuse/lifetime errors through startup, gameplay, repeated
swapchain recreation and shutdown on NVIDIA, then check Intel. Only subsequent
stability validation can link the fix to the historical aborts.

## Artifacts and preservation

`docs/bringup-artifacts/vulkan-device-loss/` contains:

- `gdb.log`: first error, native and all-thread stacks, tracker/object state.
- `api-history.json`: 96-call bounded history including the invalid submit packet.
- `runtime.log`: original `torchlight_058.log`, including the logged VUID.
- `process-maps.txt`, `binary-hashes.json`: actual loaded modules and identity.
- `capture.gdb`, `vk_layer_settings.txt`: NVIDIA and validation setup.
- `types.cpp`, `types.o`, `types-build-command.json`, `types-build.log`: debugger-only types.

The reusable observer is `tools/capture_vulkan_failure.py`. Its final GDB
configuration includes the type-object load before registering breakpoints.
The retained live capture also contains the initial setup corrections.

Pre-fix installed runtime SHA256 was
`5d0cd9a3edf983e04d7185f9bda612d911f44a01defa07ec8932646b95a6ee10` (wait fix).
Xenos SHA256 is unchanged at
`f7cca3412c33e56197c0eef5ca9b194e88cad616cd8671f06869d0c80a12ef7c` (stencil fix).

## Implemented fix and validation in progress

`patches/rexglue-present-semaphore-lifetime.patch` contains the six-file SDK
change. Present-wait semaphores are allocated per swapchain image and selected
using the acquired image index for both draw signaling and presentation waiting.
Acquire semaphores, draw command buffers and draw fences retain their existing
submission-slot ring.

Retirement now uses explicit `VK_EXT_swapchain_maintenance1` presentation fences,
tracked separately using the existing `VulkanSubmissionTracker`. Completed
fences are polled/recycled without a new per-frame host wait. Swapchain retirement
waits for draw completion and presentation-resource release before destroying
per-image semaphores. Same-swapchain/same-queue present fences signal in order;
the previous swapchain generation is drained before either changes. Present
allocation failures are not tracked as queued work; out-of-date/surface-lost
presents retain their fence tracking. If retirement cannot establish completion,
it reports a terminal failure rather than destroying potentially live resources.
See the [presentation fence resource-lifetime specification](https://docs.vulkan.org/refpages/latest/refpages/source/VkSwapchainPresentFenceInfoKHR.html)
and [present error enqueue guarantees](https://docs.vulkan.org/refpages/latest/refpages/source/vkQueuePresentKHR.html).

Both tested drivers advertise the required extension, feature and instance
surface-extension dependencies: NVIDIA GTX 1050 Ti Max-Q and Intel UHD 630.
**Compatibility limitation:** the presenter now requires this feature; older
unsupported drivers fail initialization with a specific error. No unproven
queue-idle fallback is supplied.

SDK and Debug application rebuilds pass. Fixed runtime SHA256:
`3ce56bb116f5d44c8a053cda0ad7da9881b73c5ef0ccb3ca7e9d3fb553591d46`.
Fixed Debug application SHA256:
`4cd8dca3afe5f33c8544e77cc8d96fa315265f4ecc92e7d7c69ef4b5f2b3a37e`.
The rebuilt/staged Xenos plugin is byte-identical to the verified stencil-fixed
plugin. No generated guest code or POSIX wait code changed.

Two fixed NVIDIA Debug runs completed under GDB with core/synchronization
validation and fresh matching debugger-only types. Per-API tracing is disabled
for these tests; first validation errors, GPU-loss callbacks and fatal signals
still stop for capture. Logs confirm the NVIDIA device, maintenance feature and
validation layer. Both manual correctness checks and clean exits passed.
The untraced check also completed; Intel validation was subsequently waived.
The optimized validation failure below demonstrates that device loss remains.

New artifacts: `docs/bringup-artifacts/present-semaphore-fix/`, including original
binary backups, before/after source snapshots, driver feature dumps, build logs,
fixed binary hashes and per-run validation captures.

First fixed NVIDIA Debug trial passed the user’s manual checks and exited normally
under GDB. Log span: 217.3 seconds; 7 swapchain creations including
initial setup. No Vulkan validation errors, semaphore lifetime VUIDs or device-loss
callback occurred. Independent repeats and Intel checks remain pending.

The user reports the first validation/GDB Debug trial felt substantially slower.
Debugger and synchronization-validation overhead have not been isolated yet;
a normal-launch comparison of the same fixed build is pending. No performance
regression-free claim is made from diagnostic runs.

Second fixed NVIDIA Debug trial also passed and exited normally: 126.0 seconds,
5 swapchain creations, no Vulkan validation errors or semaphore lifetime VUIDs,
and no device-loss callback. User reports approximately 20 Guest FPS from F12
in this diagnostic run. Same-build untraced comparison is now open.

Intel validation was explicitly waived by the user before launch; no fixed-build
Intel gameplay, recreation, or shutdown result is available. Extension support
alone is not a correctness validation.

The untraced fixed Debug run exited 0 and felt normal to the user. Three gameplay
windows measured 23.448 / 24.798 / 20.098 Guest FPS at 1280x720 (mean 22.781, sample
SD 2.420 within this single launch). Last-window main-thread CPU was 82.54%,
versus about 99.5% earlier, indicating the windows were not identical workloads.
All samples are retained. The earlier wait-fixed mean was 23.914 FPS; this run is
4.7% lower, and is insufficient to prove zero performance cost from the new fix.
The unusually slow subjective diagnostic behavior was not reproduced in this
normal launch. No timings or frame pacing were changed.

## Device loss after the lifetime violation is gone — NVIDIA optimized trial

User reports gameplay followed by menu/quit attempts and attempts to hide F12;
then a frozen window. GDB stopped on the GPU Commands thread at
`GraphicsSystem::OnHostGpuLossFromAnyThread(true)`, called by
`VulkanCommandProcessor::EndSubmission(bool)` (argument not recovered) via
`CommandProcessor::ExecutePrimaryBuffer`. There was no validation-error stop,
InvalidFunctionTrap, or shutdown-destructor stack. F12/menu actions are contextual
observations, not an established trigger or cause.

At 18:30:36.416 local runtime-log time, after 143.276 seconds from startup, Xenos
logs “Failed to submit a Vulkan command buffer.” Matching source and disassembly
identify the failing call as `vkQueueSubmit`. The preserved return code in R12D
is -4 (`VK_ERROR_DEVICE_LOST`); the code then marks `device_lost_=true` and enters
the captured GPU-loss callback. The debugger pauses before its fatal handling;
no actual abort signal or clean exit is claimed for this trial.

The SDK is optimized without DWARF locals. The submit packet was recovered from
its caller stack using the matching call-site disassembly (`rsp+0xf8`), not by
inventing unavailable locals. Recovered packet/state:

| Item | Value |
|---|---|
| Queue / family | `0x555559ecbf30` / graphics-compute family 0 |
| Current Xenos submission | 7695 (7693 completed, one prior submission pending) |
| Submitted command buffer | `0x7fff73e09570` |
| Command pool | `0x2940000000294` |
| Submit fence | `0x2920000000292` |
| Wait / signal semaphores | 0 / 0 |
| Previous pending Xenos fence | `0x4c200000004c2` |
| Guest frame | 2627 |
| Paint tracker | current 36683, completed 36681, pending draw 36682 |
| Present tracker | current 36683, completed 36681, pending present 36682 |
| Swapchain / image count | `0x26ca00000026ca` / 3 |
| Per-image present semaphores | `0x26ce00000026ce`, `0x26cf00000026cf`, `0x26d000000026d0` |

Command-pool reset, command-buffer begin/end and fence reset had passed the
source's success guards before this submit. This does not prove the GPU commands
were valid or that this submit originally caused the fault: asynchronous device
loss may be reported by a later call. Per-API tracing was disabled for this
validation run, so there is no complete command execution/submission history.
Core/synchronization validation was enabled; no VUID or sync hazard was reported
before loss. Known unused-fragment-output warnings remain unrelated observations.

The main UI thread is inside an NVIDIA driver ioctl under
`VulkanPresenter::PaintAndPresentImpl`, not in overlay-toggle code or shutdown.
The guest main thread is again at `sub_821A5C10`'s pointer-progress comparison
(guest sequence near `0x821A5CC4`), with LR `0x821A5CA8`, r1 `0x7018F0E0`, r9=4,
r11=6, r30=`0x3E6F`, r31=`0x4000A480`. Its less-than condition keeps waiting for
progress after GPU loss. `last_indirect_target=0`; no hint is warranted. The
kernel log around the event contains no NVIDIA Xid/GPU fault report.

Artifacts in `present-semaphore-fix/nvidia-optimized-1/`: runtime.log, gdb.log
(full/native and all-thread stacks, preserved registers and matching disassembly),
result.json, process-maps.txt, guest-context.bin, kernel.log and binary-hashes.json.
The process remains paused under GDB for inspection. No speculative F12, shader,
Xenos, pacing, stencil or wait-loop patch was made.

### Validation outcome

| NVIDIA launch | Runtime log span | Swapchain creations (including initial) | Outcome |
|---|---:|---:|---|
| Debug + validation 1 | 217.3 s | 7 | Manual checks pass; normal exit; zero validation errors |
| Debug + validation 2 | 126.0 s | 5 | Manual checks pass; normal exit; zero validation errors |
| Debug without validation/GDB | 262.6 s | 7 | Feels normal; measured counters; exit 0 |
| RelWithDebInfo + validation | 143.3 s to loss | 5 | Device loss; zero preceding validation errors; stopped for capture |

Three clean Debug exits and repeated swapchain recreation validate the narrow
lifetime fix in those tested cases. Optimized shutdown is unvalidated because
GPU loss interrupted the run. Overall GPU stability is **not established**;
RelWithDebInfo remains experimental. The first remaining concrete blocker is
Xenos queue submission reporting device loss, whose underlying GPU cause is
still unknown. The next focused investigation needs command/fault provenance
around that failure; the absence of a validation report does not prove all GPU
resource access is valid. Do not guess at F12 behavior or start a broad rewrite.

## Further inspection of the same paused process

No continue, restart, termination, inferior function call, SDK/library replacement,
or renderer edit was performed. PID 112959 remains stopped at the original
GPU-loss handler. Additional type definitions were compiled into a debugger-only
object to inspect STL elements that the original Release symbols could not
describe. These definitions were added to GDB, never loaded into the inferior.
GDB inferior function calls are explicitly disabled. Failed pretty-printer/type
lookups are retained in the log; they are not runtime failures. One debugger-only
container read was interrupted, with the inferior still paused throughout.

### A. Confirmed submission and ownership state

The last successfully **enqueued Xenos** submission is 7694, not 7693:
7693 is the last completion observed by Xenos's fence tracker. Submission 7694
remains in `command_buffers_submitted_`; the success-only insertion path proves
its vkQueueSubmit returned VK_SUCCESS. Its command pool is `0x4c300000004c3`,
command buffer `0x7fff141f4610`, and fence `0x4c200000004c2`. It uses the same
family-0 queue `0x555559ecbf30`. Xenos submits no signal semaphores here; its
pending semaphore list is empty. By the success-path tracking code, no retained
wait semaphore belongs to this pending submission. Its full VkSubmitInfo and
recorded commands were not retained. Presenter work can interleave on the same
queue, so this is not a claim about the immediately previous global queue call.

Failed submission 7695 is still backed by the last element of the writable pool
list: pool `0x2940000000294`, buffer `0x7fff73e09570`, fence `0x2920000000292`.
These differ from 7694's pending objects. The writable list contains eight pools;
Xenos uses a pool/free-list allocator here, not the presenter's three-slot ring.
The fence remains in `fences_free_` because the failure prevented success-path
transfer to the pending list. This bookkeeping state is expected on this branch;
it does not itself indicate that a successfully submitted fence was reused.
Host-side command pool reset, begin/end recording, and fence reset passed their
success guards. No Vulkan driver command-buffer state query was attempted.

Guest frame is 2627 (frame-resource ring index 2), completed frame is 2625;
closed-frame submission slots contain `{7690, 7693, 7687}`. The main guest remains
in the previously captured progress wait. No new InvalidFunctionTrap occurred.

The presenter is concurrently **inside another vkQueueSubmit**, not
vkQueuePresentKHR or teardown. Recovered packet at its caller stack `rsp+0x1a0`:
queue `0x555559ecbf30`, draw submission 36683, ring slot 2, acquired image index 0
(inferred from the selected per-image signal semaphore). It waits on acquire
semaphore `0x100000000010`, signals image-0 semaphore `0x26ce00000026ce`, submits
command buffer `0x55555a434dd0`, and uses fence `0xe100000000e1`. This call has
not returned. The acquired image belongs to current swapchain `0x26ca00000026ca`;
all three present semaphores match that generation. Prior draw/present 36682 is
pending; draw/present trackers completed 36681. No semaphore handle from this
packet occurs in the Xenos failure packet, which has no wait or signal semaphores.
Xenos released the queue acquisition lock before logging/reporting its error,
allowing the presenter to enter that same queue while the callback is reached.
Thus two stacks containing submission code do not establish unsynchronized host
access to the queue. Prior successful presenter packets are unavailable.

### Retained commands and resources

`DeferredCommandBuffer::Execute` does not clear its source stream; reset happens
when the next submission starts. The entire failed stream was therefore recovered:
101,112 bytes, 1,337 commands, fully decoded with validated record boundaries.
It includes 85 balanced dynamic-rendering scopes, 96 draws (86 indexed),
74 compute dispatches, 182 buffer-copy commands, 23 buffer-to-image copies,
312 barriers and three attachment clears. No malformed record or rendering-scope
imbalance was found. These are **recorded commands**, not proof of GPU execution.

The final operations, using zero-based decoded command indices, are:

| Commands | Recorded operation |
|---|---|
| 1317–1324 | Color target dump to EDRAM, pipeline `0x2ae00000002ae`, dispatch 80×90×1 |
| 1325–1329 | Full-32bpp EDRAM resolve to shared memory, pipeline `0x520000000052`, dispatch 20×90×1 |
| 1330 | Color target transitions SHADER_READ_ONLY → COLOR_ATTACHMENT_OPTIMAL |
| 1331–1333 | Clear depth/stencil over 640×720, depth=0, stencil=0 |
| 1334–1336 | Clear color over 640×720, RGBA approximately (0.1098, 0.1922, 0.1843, 1) |

The color render-target image is `0x2a200000002a2`, view `0x2a400000002a4`,
allocation `0x2a300000002a3`; it is a guest 2×MSAA target, base tile 0, pitch 8.
Depth image is `0x29c000000029c`, combined view `0x29f000000029f`, allocation
`0x29d000000029d`, guest 2×MSAA, base tile 736, pitch 8. Both remain in the live
render-target map and their tracked layouts agree with the final recorded usage.
The 640×2048 rendering area covers the cache's EDRAM target; it is not a change
to the guest/internal 1280×720 resolution. The decoded 640×720 clear rectangles
are within that rendering area.

EDRAM buffer is `0x3e000000003e`; guest shared-memory buffer is
`0x360000000036`. The final compute pipeline is the existing
`resolve_full_32bpp_cs` entry, not a new direct-image resolve shader. The source's
“direct resolve” preflight still delegates to the EDRAM dump path. Descriptor
set `0x46110000004611` supplies the resolve destination, with current
`last_written_range_` `[0x1f360000, 0x1f6e4000)` (length `0x384000`), inside the
512-MiB shared buffer. Its actual driver descriptor contents cannot be queried
from this capture; the range association is from Xenos's retained state/source.
Last guest graphics pipeline is `0x1830000000183`, with vertex shader hash
`acbb12cdc23a7ff7`, pixel shader hash `7abdfefbcadb4bf9`; the cache marks it as a
real pipeline, not an asynchronous placeholder. The exact shader/pipeline data
is preserved in `ownership.json`.

### Strongest concrete defect: compute writes classified as reads

`VulkanSharedMemory::GetUsageMasks`, SDK `src/graphics/vulkan/shared_memory.cpp`
lines 338–341, maps `Usage::kComputeWrite` to COMPUTE_SHADER stage and
**VK_ACCESS_SHADER_READ_BIT (0x20)**, omitting SHADER_WRITE (0x40). The loaded
plugin's disassembly independently confirms that branch returns stage 0x800,
access 0x20. This is not an inference from possibly stale source alone.

The captured command 1328 contains exactly this shared-buffer barrier: stages
0x18bc → 0x800, access 0x822 → 0x20. Command 1329 then dispatches the resolve
shader that writes destination storage-buffer set 1. The checked-in SPIR-V
identifies that binding as NonReadable and contains the destination OpStore.
At the pause, `last_usage_` is kComputeWrite with the written range above.
`Use` also uses GetUsageMasks for the **source** side when leaving this usage,
so subsequent dependencies omit those shader writes from availability operations.
This is a concrete synchronization-model defect and the strongest actionable
candidate found in the paused inspection. Adding SHADER_WRITE to that usage mask
is the narrow candidate correction; **it has not been applied**.

Crucial causality limit: an incoming read→write transition needs an execution
dependency, so the read-only mask in command 1328 does not by itself prove an
actual hazard at that point. No subsequent shared-memory consumer occurs after
the final resolve in this retained stream. The missing outgoing write dependency
must be correlated across submissions to establish a concrete hazardous overlap.
The preceding stream has already been overwritten, and the GPU fault location is
unknown. Accordingly, this defect is **not a proven cause of VK_ERROR_DEVICE_LOST**.
The [Khronos synchronization examples](https://docs.vulkan.org/guide/latest/synchronization_examples.html)
distinguish read→write execution ordering from the write→read memory dependency
that requires the write access mask.

### B. Checks that reduced the candidate set, and their limits

- No overlap between the pending 7694 command pool/buffer/fence and 7695's chosen
  objects; no current bookkeeping evidence of recycling those while in flight.
  This cannot certify all historical reuse or the driver's internal state.
- No present-semaphore reuse/lifetime VUID preceded loss. Current per-image
  semaphore ownership is consistent. This does not rule out every possible
  presenter/guest-output race.
- CP image/view/buffer/framebuffer/memory retirement queues and pipeline deferred
  destruction queue are empty. The last targets/pipelines are still live.
  Already-completed destruction history is absent, so historical use-after-free
  cannot be ruled out.
- Of 242 descriptor-set handles bound in the stream, 237 are in current transient
  usage lists, all tagged frame 2627. Of the remaining five, persistent EDRAM
  set `0x410000000041` and shared-memory/EDRAM set `0xa100000000a1` are
  independently identified; three other bindings are associated with render-target
  transfer/dump commands but their allocator ownership was not fully decoded.
  No bound descriptor is in the recovered constant or single-buffer free lists.
  Texture free-list decoding remained incomplete due debug-type ambiguity;
  actual driver descriptor contents and writes before this submission are unknown.
- The three staging buffers used for shared-memory uploads are owned by the
  pool and tagged submission 7695: `0x3a650000003a65`, `0x3af20000003af2`,
  `0x3b3d0000003b3d`. Maximum source ends are 2,097,152 / 2,097,152 / 1,171,456
  bytes, within their 2-MiB pages. The first two pages are in the submitted/full
  list; the last current page has used=flushed=1,171,456. No basic upload overrun
  or unflushed current-page tail is evident. Earlier contents and host/GPU
  timing are not reconstructed.
- No contradictory old/new-layout chain was found for matching explicit
  image/subresource ranges within this stream. Final tracked target layouts
  match recorded usage. This is not a complete subresource-overlap, descriptor,
  sparse residency, memory-aliasing, or cross-submission validation.
- F12 and teardown are absent from the failure stacks. Their involvement as
  triggers is unproven, not ruled out merely from this snapshot.

### C. Remaining hypotheses, ranked by evidence

1. **Shared-memory resolve-write visibility defect.** Concrete source, loaded
   machine code and barrier evidence agree; a missing SHADER_WRITE scope can
   break later dependencies. The actual overlapping consumer/fault is missing.
2. **Other GPU data/resource access in pending Xenos work.** Texture conversion,
   guest shader access, EDRAM dump/resolve or sparse/descriptor lifetime could
   fault outside the checks captured here. Detailed commands are available for
   7695, but not 7694; no particular out-of-bounds access is established.
3. **NVIDIA driver execution fault.** Device loss without a validation diagnostic
   leaves this possible, but there is no Xid, fault address, or executed-command
   marker supporting a driver-specific cause over an application defect.
4. **Presenter resource-generation race.** Lower priority: per-image ownership is
   coherent and tested reuse violations are gone; concurrent queue calls are
   explained by the existing lock/release ordering. Shared output-image history
   remains incomplete, so it is not absolutely excluded.

### D. Smallest next instrumentation proposal — not implemented

Add an opt-in bounded submission journal retaining the previous few Xenos
streams and submit results, plus shared-memory Usage transitions with actual
stage/access masks, written ranges and resolve destination descriptor
buffer/offset/range. Include draw/present submission IDs so shared-queue ordering
can be reconstructed. Preserve entries until their fences complete, and keep
recent completed entries long enough to cover delayed loss. This supplies the
missing producer/consumer evidence for the concrete mask defect without changing
timing, masks, shaders, or synchronization behavior. Do not add queue-idle waits
or speculative renderer fixes for diagnosis.

For GPU execution localization, the smallest additional GPU-side measure is
opt-in `VK_NV_device_diagnostic_checkpoints` markers at submission start/end and
major texture-upload/draw/resolve phases, with stable marker storage. Query the
last completed markers on device loss and map them to the journal. NVIDIA
advertises this extension (and EXT_device_fault), but neither was enabled on the
current device; useful markers cannot be retroactively recovered. Its purpose
and limits are documented in the [Vulkan diagnostic checkpoint extension](https://docs.vulkan.org/refpages/latest/refpages/source/VK_NV_device_diagnostic_checkpoints.html).
This is a proposal for a later run, not authorization to discard this paused one.

All additional evidence is under
`present-semaphore-fix/nvidia-optimized-1/paused-inspection/`: raw/decoded stream,
ownership snapshots, render-target records, layout/descriptor consistency checks,
source/disassembly copies, extra type definitions and final paused state. The
main GDB log contains the exact live observations. `tools/inspect_paused_vulkan.py`
is a host-side debugger extractor only. Root cause of the device-loss event
remains unproven; the strongest narrow defect is now identified for review.
