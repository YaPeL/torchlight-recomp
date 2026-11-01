// Diagnostics: the store trace's recorder interface (diag/store_trace.h is the generated code's
// side, diag/store_trace.cpp the recorder).

#pragma once

#include <atomic>
#include <cstdint>

struct PPCContext;

namespace torchlight::diag::store_trace {

// Set while any thread is inside a traced call (the fast check of every store).
// Threads inside a traced call (or a compared one): the generated code's stores call in only while
// it is not zero.
extern std::atomic<int> g_threads_inside;

// A store of `size` bytes (1, 2, 4, 8) of `value` (as the guest sees it, before the byte swap) at
// guest address `address`, about to be made by `function`.
void Store(const uint8_t* base, uint32_t address, uint32_t size, uint64_t value,
           const char* function);
// A raw access (load or store) of up to 128 bytes at `address`: its line is snapshot on first
// touch in the call and compared when the call ends.
void RawTouch(const uint8_t* base, uint32_t address, const char* function);
// A store to the device registers (REX_MM_STORE_* in the MMIO range): it cannot be undone.
void MmioStore(uint32_t address, const char* function);

// SceneManager::renderSingleObject's call, from its entry (the trace's own, or a native path's
// hook): with --diag_store_compare, `candidate` against `original` (the guest's function); with
// --diag_store_trace, `original` traced. False when neither is on (the hook runs the call).
using GuestFunction = void (*)(PPCContext&, uint8_t*);
// True on the thread while a comparison runs the guest's function (the reference) or the trace
// runs it. The hooks that check it (hooks/native_skeleton.cpp) then run the guest's code too, so
// the reference holds none of their native code; the other host hooks (guest copies, bucket
// culling) run in both runs, being the same in both.
inline thread_local bool t_reference_run = false;
struct ReferenceRun {
  bool previous = t_reference_run;
  ReferenceRun() { t_reference_run = true; }
  ~ReferenceRun() { t_reference_run = previous; }
};
bool RenderSingleObject(PPCContext& ctx, uint8_t* base, GuestFunction candidate,
                        GuestFunction original);

// Any native function's call, with --diag_store_compare and --diag_store_compare_native:
// `candidate` (the native code) against `original` (the guest's function) by the same comparison
// (memory other than the stack, r1, and the `registers` given), entry `entry`, on the render thread (the one
// renderSingleObject was first compared on) and not inside another traced or compared call. False
// when the comparison does not run (the hook runs the call).
// The registers a native function's comparison checks besides r1: its results (a void function's
// r3 and f1 are whatever its last callee left).
enum CompareRegisters : uint32_t { kCompareR3 = 1, kCompareF1 = 2, kCompareResults = 3 };
bool CompareNativeCall(PPCContext& ctx, uint8_t* base, uint32_t entry, GuestFunction candidate,
                       GuestFunction original, uint32_t registers = kCompareResults);

}  // namespace torchlight::diag::store_trace

