// Diagnostics: the store trace's recorder interface (diag/store_trace.h is the generated code's
// side, diag/store_trace.cpp the recorder).

#pragma once

#include <atomic>
#include <cstdint>

namespace torchlight::diag::store_trace {

// Set while any thread is inside a traced call (the fast check of every store).
extern std::atomic<bool> g_active;

// A store of `size` bytes (1, 2, 4, 8) of `value` (as the guest sees it, before the byte swap) at
// guest address `address`, about to be made by `function`.
void Store(const uint8_t* base, uint32_t address, uint32_t size, uint64_t value,
           const char* function);
// A raw access (load or store) of up to 128 bytes at `address`: its line is snapshot on first
// touch in the call and compared when the call ends.
void RawTouch(const uint8_t* base, uint32_t address, const char* function);

}  // namespace torchlight::diag::store_trace

