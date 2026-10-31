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

// SceneManager::renderSingleObject's call, from its hook (hooks/native_render_object.cpp): with
// --diag_store_compare, `candidate` against `original` (the guest's function); with
// --diag_store_trace, `original` traced. False when neither is on (the hook runs the call).
using GuestFunction = void (*)(PPCContext&, uint8_t*);
// True on the thread while the comparison runs the guest's function (the reference): the native
// paths' other hooks (hooks/native_render_object.cpp) then run the guest's code too, so the
// reference is the guest's all the way down.
inline thread_local bool t_reference_run = false;
bool RenderSingleObject(PPCContext& ctx, uint8_t* base, GuestFunction candidate,
                        GuestFunction original);

}  // namespace torchlight::diag::store_trace

