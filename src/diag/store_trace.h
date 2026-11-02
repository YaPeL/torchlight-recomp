// Diagnostics: the store trace (docs/perf/native-render-queue.md section 4). Precompiled into the
// generated code's target after its own torchlight_pch.h (CMakeLists.txt, only with
// TORCHLIGHT_DIAGNOSTICS), it redefines the generated code's guest memory stores (REX_STORE_U8..64)
// and raw addresses (REX_RAW_ADDR: vector stores, dcbz, atomics) so that, inside the call tree of a
// traced guest function (diag/store_trace.cpp), every write is recorded with its address, size, old
// and new value and the recompiled function that made it. Outside a traced call each store costs
// one relaxed load and a predicted branch. Nothing generated and nothing in the SDK is edited.

#pragma once

#include "torchlight_pch.h"

#include "diag/store_trace_api.h"

#define TORCHLIGHT_STORE_TRACE(x, size, y)                                                       \
  (__builtin_expect(torchlight::diag::store_trace::g_threads_inside.load(std::memory_order_relaxed) != 0, 0)  \
       ? torchlight::diag::store_trace::Store(base, (u32)(x), (size), (uint64_t)(y), __func__)   \
       : (void)0)

#undef REX_STORE_U8
#undef REX_STORE_U16
#undef REX_STORE_U32
#undef REX_STORE_U64
#undef REX_RAW_ADDR
#undef REX_MM_STORE_U8
#undef REX_MM_STORE_U16
#undef REX_MM_STORE_U32
#undef REX_MM_STORE_U64
#define REX_STORE_U8(x, y) \
  (TORCHLIGHT_STORE_TRACE(x, 1, y), \
   *(volatile u8*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)) = (y))
#define REX_STORE_U16(x, y) \
  (TORCHLIGHT_STORE_TRACE(x, 2, y), \
   *(volatile u16*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)) = __builtin_bswap16(y))
#define REX_STORE_U32(x, y) \
  (TORCHLIGHT_STORE_TRACE(x, 4, y), \
   *(volatile u32*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)) = __builtin_bswap32(y))
#define REX_STORE_U64(x, y) \
  (TORCHLIGHT_STORE_TRACE(x, 8, y), \
   *(volatile u64*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)) = __builtin_bswap64(y))
#define REX_RAW_ADDR(x)                                                                          \
  ((__builtin_expect(torchlight::diag::store_trace::g_threads_inside.load(std::memory_order_relaxed) != 0, 0) \
        ? torchlight::diag::store_trace::RawTouch(base, (u32)(x), __func__)                     \
        : (void)0),                                                                              \
   (base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)))
// The stores that may reach the device registers: memory stores traced as above; a register store
// is marked (it cannot be undone) and passed on.
#define TORCHLIGHT_MM_STORE(addr, val, size, store)                                             \
  do {                                                                                          \
    u32 _mmio_addr = (addr);                                                                    \
    u64 _mmio_val = static_cast<u64>(val);                                                      \
    if (REX_IS_MMIO_ADDR(_mmio_addr)) {                                                         \
      if (__builtin_expect(                                                                     \
              torchlight::diag::store_trace::g_threads_inside.load(std::memory_order_relaxed),  \
              0))                                                                               \
        torchlight::diag::store_trace::MmioStore(_mmio_addr, __func__);                         \
      if ((size) == 8) {                                                                        \
        rex::runtime::MMIOHandler::global_handler()->CheckStore(                                \
            _mmio_addr, static_cast<u32>(_mmio_val >> 32));                                     \
        rex::runtime::MMIOHandler::global_handler()->CheckStore(_mmio_addr + 4,                 \
                                                                static_cast<u32>(_mmio_val));   \
      } else {                                                                                  \
        rex::runtime::MMIOHandler::global_handler()->CheckStore(_mmio_addr,                     \
                                                                static_cast<u32>(_mmio_val));   \
      }                                                                                         \
    } else {                                                                                    \
      store(_mmio_addr, _mmio_val);                                                             \
    }                                                                                           \
  } while (0)
#define REX_MM_STORE_U8(addr, val) TORCHLIGHT_MM_STORE(addr, val, 1, REX_STORE_U8)
#define REX_MM_STORE_U16(addr, val) TORCHLIGHT_MM_STORE(addr, val, 2, REX_STORE_U16)
#define REX_MM_STORE_U32(addr, val) TORCHLIGHT_MM_STORE(addr, val, 4, REX_STORE_U32)
#define REX_MM_STORE_U64(addr, val) TORCHLIGHT_MM_STORE(addr, val, 8, REX_STORE_U64)
