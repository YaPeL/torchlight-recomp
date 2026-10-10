# Crash handling for the release (design, 2026-10-08)

Goal: when the game hits an error nothing can recover from, it writes a report to the player's log
folder, tells the player in one short message where the report is, and exits. It must never loop,
and it must never fill the disk with logs. This should work the same way on Linux (with the Steam
Deck), Windows and macOS.

This is a design; CR.1 (section 9) is implemented. The facts about the SDK are from `bd833a2`, the
base `develop` uses since 2026-10-08, unless a line says `0c7b01a` (the base before it). "Seen" means a run or a
test; "read" means only the code was read.

## 1. What happens today

| Event | Linux and macOS | Windows |
|---|---|---|
| Host fault that no SDK handler claims | **Endless loop.** `ExceptionHandlerCallback` (`exception_handler_posix.cpp`) returns without chaining to anything, so the instruction faults again. If the address is guest memory outside the physical heaps, `Memory::AccessViolationCallback` logs `Unhandled guest access violation` on every retry. Seen: mods runs 117, 129, 130 and 161 (`docs/mods.md` 7e on `feature/pc-mods`), mods run `torchlight_084` (2026-10-09: a guest read of null+0xB4, 6010 identical lines in 89 s until the run cap stopped it), and a test here (draft D25) | The vectored handler returns `EXCEPTION_CONTINUE_SEARCH`. No filter of ours exists, so the process ends through Windows Error Reporting with no message from us (read) |
| Guest `RtlRaiseException`: a C++ `throw` (0xE06D7363) or any code but SetThreadName (0x406D1388) | `rex::debug::Break()`. **Its first call returns**: it installs a SIGTRAP handler that only resets the signal to its default, so the guest carries on past a call that never returns. A second `Break()` kills the process with SIGTRAP (core dump, no message) (read) | `__debugbreak()`: unhandled breakpoint, process ends through WER (read) |
| `KeBugCheck`/`KeBugCheckEx` | Same `Break()`, then `assert_always` | Same |
| `DbgBreakPoint` | Same `Break()`. Torchlight's 1114 callers are OGRE's `OGRE_EXCEPT` sites, which run on with the state the error was about (read; SDK patch 31) | Same |
| `RtlUnwind`, `RtlCaptureContext`, `__C_specific_handler` | Stubs: they log `[STUB] ... not implemented` and return. `RtlUnwind` runs once per start at the title screen with no visible effect (draft D17) | Same |
| Our code, OGRE or the SDK faulting on a host address | Same endless loop as the first row: the SDK returns `false` before any guest check | WER |

Logs:
- `0c7b01a`: a rotating file sink, 5 MB × 20 files per run (`log_max_file_size_mb`,
  `log_max_files`). Each run gets a new number, and old runs are never removed. This machine's log
  folder (`~/.local/state/TorchlightRecomp/logs`) holds 447 files, 1.1 GB.
- `bd833a2` (`b971840`): a plain `basic_file_sink`, so a run had **no size limit at all**, until
  our SDK patch 26 (`rexglue-log-rotation.patch`) brought the rotating sink and its two cvars back.
  `LogConfig::dir_budget_bytes` prunes old runs, but only when the runtime picks the name itself.
  Our layer sets `log_file` (`src/live/install.cpp`, `ConfigurePaths`), so on the new base an
  endless loop would grow one file without bound.

## 2. When the game counts as crashed

The rule: **cut only what cannot recover today.**

**A. Host faults (SIGSEGV, SIGBUS, SIGILL, SIGFPE; Windows access violation, illegal instruction,
stack overflow).**
- Today a fault recovers only when an SDK handler claims it. The claiming handlers are MMIO, the
  physical heap callbacks (write watches) and the stale-protection recovery in
  `AccessViolationCallback`. All of them run before a fault counts as unclaimed, so the criterion
  never sees a fault that recovers today.
- An unclaimed fault re-executes the same instruction with the same state, so it only succeeds if
  another thread changes the mapping in the meantime. As insurance against that race, the reporter
  lets the same fault retry a few times:
  - On the first unclaimed fault, it notes (thread, host instruction address, faulting address) and
    returns after 1 ms.
  - Fatal: the same triple faults unclaimed 3 times in a row. That costs at most about 3 ms before
    the report.
  - Any other fault on that thread resets the count.
- A fault whose address is outside guest memory (a host null or wild pointer: our backend, OGRE,
  the SDK) is fatal at once. No other thread maps host memory under us.
- SIGILL from a `REX_UNIMPLEMENTED` trap is fatal with that reason. Torchlight's generated code has
  none today.

**B. Guest exceptions.**
- `RtlRaiseException` with any code other than SetThreadName is fatal, C++ `throw` included.
  Nothing dispatches it to a handler. Continuing runs whatever code follows a call that should not
  return. With a debugger attached, `Break()` comes first, for development.
- `KeBugCheck`/`KeBugCheckEx` is fatal.
- `DbgBreakPoint` is fatal. In Torchlight it is how Runic's OGRE raises its errors
  (`OGRE_EXCEPT`): the code after it runs on with the error's state, and a second one already
  kills the game today.
- `RtlUnwind` is **not** fatal. It is the local unwind of a normal path, seen every start without
  effect. Its message is logged once per call site (the guest's `lr`), and the report lists it among
  the recent events if a crash follows. This is revisited when D17 is implemented.

**C. Our own fatal paths.** `std::terminate`, `abort` (a failed `assert` in a debug build) and
`rex::FatalError` (which prints to stderr and calls `abort`). They are caught as SIGABRT on every
platform; on Windows `abort` raises it through the C runtime's `signal`, not through SEH, so the
filter alone does not see it.

**Not detected:** guest infinite loops and deadlocks. They write no logs (the disk is safe), and a
watchdog on presented frames would misfire during loading screens. Left for later.

## 3. The report

**File.** One text file per crash in the player's log folder (`platform::LogDir()`), named after
the run's log: `torchlight_<NNN>_crash.txt`. It is plain text: a player can read it, and a script
can resolve it. Paths under the home folder are written as `~`.

**Sections:**
1. **Build:** version (the tag, `git describe`), `TORCHLIGHT_BUILD_COMMIT`, the SDK commit and the
   patch series key (`tools/deps/key.sh`, written at build time), OS and version, CPU, render mode
   (`--native_live`), time and uptime.
2. **Reason:** kind (host fault, guest exception, bug check, abort, unimplemented instruction),
   signal or exception code, faulting address (host, and guest when it falls in guest memory), read
   or write, and how many times it repeated.
3. **Thread:** host thread id; guest thread id and name (`XThread`); whether it is the main thread.
4. **Guest registers:** the faulting thread's `PPCContext` (`ThreadState::context()`): r0-r31,
   f0-f31, lr, ctr, cr, xer, fpscr.
   - Registers the manifest keeps in host locals (`cr`, `ctr`, `xer` and the reservation since
     `b0b2bbb`) are stale in `ctx`, and the report says so.
   - The values of r1 and lr are what was last stored before the fault.
5. **Guest stack:** two sources, each labelled.
   - **Generated functions on the host stack.** After symbolization, each frame named
     `sub_8XXXXXXX` is a guest function, exactly. This is the reliable one.
   - **The guest back chain** from `ctx.r1`: the back pointer at `0(r1)`, and each function's
     saved `lr` at `back - 8`. That slot is where `__savegprlr_N` and the prologues store `r12`
     after `mflr` (read in the generated code; confirm on a test crash). Every read checks that the
     address is mapped guest memory first. Up to 64 frames.
   - Both are guest addresses only: nothing of the game's data goes into the report.
6. **Host stack:** the faulting thread's return addresses, up to 64, each as module + offset.
7. **Modules:** path, load address and identity of each loaded module: the GNU build ID on Linux,
   the LC_UUID on macOS, the PDB GUID and age on Windows. Release binaries are stripped, so this is
   what the symbols are matched by.
8. **Last log lines:** the last 200 lines from an in-memory ring (spdlog `ringbuffer_sink`), and
   how many duplicates the log suppressed (section 5).

**Resolving it later** (a script, `tools/crash/resolve.py`, to be written). It takes the report and
a checkout of `torchlight-symbols`, and finds `<tag>/<platform>/` by version:
- Linux: `addr2line -f -C -e <the .debug matching the build ID>` on each module + offset.
- Windows: `llvm-symbolizer` with the PDB.
- macOS: `atos` with the dSYM, which the macOS release would add to `torchlight-symbols`.

The resolved `sub_` names give the guest stack, and the guest addresses can then be looked up in
`guest_abi` and the legacy notes by hand.

## 4. Writing the report safely

The faulting thread may hold any lock (malloc, the log's mutex, SDL's), so the handler itself does
almost nothing:

- **At startup**, our layer:
  - starts a **reporter thread** that waits on a pipe (Linux and macOS) or an event (Windows);
  - formats the build section once;
  - opens the log folder.
- **In the handler** (POSIX: our `sigaction` handler, installed *before* the SDK's so that D25 calls
  it; Windows: `SetUnhandledExceptionFilter`):
  - applies the criterion of section 2;
  - copies the signal information and the CPU context to a static buffer;
  - wakes the reporter thread, then waits for it with a timeout of 5 s.
- **The reporter thread:**
  - unwinds the faulting thread from the copied context: libunwind's `unw_init_local` with the
    `ucontext` on Linux and macOS, `StackWalk64` with the exception `CONTEXT` on Windows;
  - reads the guest state;
  - takes the ring buffer's lines with a `try_lock` (it skips them rather than deadlock);
  - writes the file with plain `open`/`write` (`CreateFileW`/`WriteFile`).
- **Then** the handler starts the message helper (section 6) and ends the process:
  - `_exit(128 + signal)` on POSIX, so release builds write no core of a process with gigabytes of
    guest memory mapped;
  - `TerminateProcess` on Windows, so WER adds no dialog of its own.
- If anything in the reporter fails or times out, the process still ends: no hang in any case.
- Development builds re-raise to the default action instead, keeping core dumps and debuggers.
- Windows also writes `torchlight_<NNN>_crash.dmp` with `MiniDumpWriteDump` (`MiniDumpNormal`:
  stacks and modules, no process memory), from the reporter thread.
- **Stack overflow:** the SDK creates the guest threads without an alternate signal stack, so on
  POSIX an overflow cannot run the handler and the process just dies, which is not a hang. A
  `sigaltstack` per guest thread (an SDK patch, section 7) would let it write the report too.

Considered and set aside: Crashpad, or Breakpad through sentry-native. They are robust and
out-of-process, with minidumps on all three platforms. But they add a large dependency to the deps
pipeline, a minidump toolchain to resolve reports, and an upload model we do not want. To revisit
if the in-process reports turn out unreliable.

## 5. Keeping the log bounded

- **Repeat suppression:** the file sink goes behind spdlog's `dup_filter_sink` (5 s window). A
  message repeated in a burst is written once, then `Skipped N duplicate messages`. It only
  catches identical lines: a burst that differs by an argument passes through. The largest one
  seen, a mod pack's thousands of missing-file opens (a path each), is gone at its source: SDK
  patch 28 logs them at DEBUG.
- **Per run, at most 50 MB:** a rotating sink of 5 MB × 10, set through the two cvars
  (`src/live/log_budget.h`). On `bd833a2` the rotation is our SDK patch 26 (section 7).
- **The folder, at most 200 MB:**
  - At startup our layer removes the oldest runs (each run's log, its rotations, its OGRE log and
    its crash report) until the folder fits.
  - It always keeps the last 10 crash reports.
  - On `bd833a2` this could move to the runtime's `dir_budget_bytes` if our layer stops setting
    `log_file` and lets the runtime name the run (the same `<app>_NNN.log` that `NextLogPath`
    copies). The OGRE logs and the crash reports stay ours to prune.
- With section 2 in place an endless fault loop cannot happen, and the limits above bound
  everything else.

## 6. What the player sees

- After the report, the dying process starts **the same executable in a helper mode**, passing it
  the report's path; it does not show anything itself. The helper shows one SDL message box:
  > Torchlight stopped because of an error. A report was saved to `<path>`. Please attach it if you
  > report the problem.

  It has two buttons, "Open folder" and "Close", and exits. Why a helper:
  - the crashed process's window, GL context and SDL state are suspect;
  - a signal handler cannot show UI;
  - macOS only shows UI from the main thread.
- **Next start:** if the last run left a report the player has not seen, the launcher or the
  first-start UI shows the same message once. This covers Steam Deck Gaming Mode (gamescope may not
  show a message box from a helper process) and any helper that failed to start.
- The text goes through `docs/translations.md` like the rest of the UI.

## 7. Who does what

**Our layer.** A crash module in `src/platform/` (POSIX shared by Linux and macOS, Windows apart, a
common interface), following CLAUDE.md's platform rule:
- the handler and its criterion, the reporter thread and the report writer;
- the helper mode and the next-start notice;
- the log sinks (`OnConfigureLogging`: ring buffer, duplicate filter) and the
  folder pruning;
- the build section, with the version, the series key and the commit added to `build_info.h`;
- `tools/crash/resolve.py`.

**SDK patches** (in the `bd833a2` series):
1. **D25, unclaimed faults chain to the previous handler** (patch 30), without uninstalling the SDK's (the
   draft was revised so). This is what makes section 2.A possible on POSIX. Windows needs nothing:
   `EXCEPTION_CONTINUE_SEARCH` already reaches our filter.
2. **A guest fatal-error hook** (patch 31, draft D31; `DbgBreakPoint` included).
   - `RtlRaiseException` (every code but SetThreadName), the C++ throw path and `KeBugCheckEx` call
     a handler the app registers, with the exception record and the thread's `PPCContext`.
   - Without a registered handler: log the record and `abort()`, never return to the guest.
   - Today they call `Break()`, which returns on POSIX.
   - The C++ alternative, an executable's `__imp__RtlRaiseException` interposing the runtime's,
     would work on Linux and fail on Windows, where the runtime is a DLL.
3. **Log rotation on `bd833a2`.** Done: patch 26 brings back the rotating sink and
   `log_max_file_size_mb`/`log_max_files`. Its removal was a regression from `0c7b01a`. The
   duplicate filter still needs a way to wrap the runtime's file sink (a `LogConfig` field).
4. **(Optional) `sigaltstack` for guest threads**, so stack overflows get a report on POSIX.

**Upstream**, proposed together with D17 (`RtlUnwind`) as "fatal guest errors":
- D25 as revised.
- Patch 2, the fatal hook, with its default (`abort` instead of a `Break()` that returns).
- `Break()` returning on POSIX, which is its own bug.
- Patch 3, the log size regression.
- Patch 4.

D17's local unwind, once implemented, removes the only stub the game hits on a normal path.

## 8. Tests

- **Unit:**
  - the criterion (the same triple 3 times is fatal; a different fault resets; an unclaimed fault
    outside guest memory is fatal at once);
  - the report's formatting (sections, `~` paths, module lines);
  - the folder pruning (budget, the last 10 reports kept);
  - the duplicate filter.
- **In a child process, per platform, in CI:**
  - a host null read ends the child within 1 s, with the report written and the expected exit code;
  - a guest read of an unmapped address does the same;
  - an MMIO access in the same child still works, so nothing that recovers today is cut.
- **Guest exceptions:** a PPC test that calls `RtlRaiseException` with a C++ code ends the child
  with a report. The test needs the hook (SDK patch 2).
- **One game run, agreed beforehand,** per platform with a deliberate crash (the command-line
  option of section 10), to check the message, the next-start notice and the guest back chain's
  layout.

## 9. Order

1. **CR.1:** log limits (section 5) and the duplicate filter. This is independent of the rest and
   ends the disk problem first. **Done** (`fix/log-budget`): the folder pruning and the rotation
   cvars, on both bases, with SDK patch 26 on `bd833a2`; the duplicate filter is left for patch 3's
   follow-up.
2. **CR.2:** SDK patches 1 and 2 (with D25's test). **Written** (2026-10-10, `sdk/fatal-errors`):
   SDK patches 30 and 31, with their tests.
3. **CR.3:** the POSIX handler, the reporter and the report (Linux), with the child-process tests.
4. **CR.4:** Windows: the filter, `StackWalk64` and the PDB identity.
5. **CR.5:** the helper mode and the next-start notice.
6. **CR.6:** `tools/crash/resolve.py`, checked on a report from each platform.
7. **CR.7:** macOS, with the macOS port (`docs/macos-port.md`), on the same POSIX code.

## 10. Decisions (owner, 2026-10-08)

1. **A crash on request, as a command-line option** (not a cvar of the menu), available in the
   release too, so that a tester can check that the report is written and resolves with the
   symbols. It is documented in the development docs only (`docs/BUILDING.md`), not in the
   player's.
2. **No core dump in the release, a core in development builds.**
3. **50 MB a run, 200 MB a folder, the last 10 crash reports.**
4. **A minimal minidump on Windows** next to the report: the threads' stacks and the module list,
   none of the process's memory (`MiniDumpNormal`).

The order of section 9 stands: the log limits first (CR.1).
