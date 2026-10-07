// The guest's 32-bit address space as the recompiled code reaches it: where host translation stops
// being one linear offset, and where accesses are device registers rather than memory.

#pragma once

#include <cstdint>

namespace torchlight::guest_abi::xbox_memory {

// Device registers (MMIO): the generated code treats [0x7F000000, 0x80000000) as MMIO
// (REX_IS_MMIO_ADDR in generated/default/torchlight_pch.h); the SDK maps the GPU registers at
// 0x7FC80000-0x7FCFFFFF (graphics_system.cpp, AddVirtualMappedRange) and serves a plain host access
// there by decoding the faulting x86 instruction (mmio_handler.cpp), which only knows simple moves.
inline constexpr uint32_t kMmioBegin = 0x7F000000u;
inline constexpr uint32_t kMmioEnd = 0x80000000u;

// Host translation: base + address, plus 0x1000 from this address up on Windows and macOS arm64
// (REX_PHYS_HOST_OFFSET in generated/default/torchlight_pch.h, rex::memory::GuestPtr). A range on
// one side of it is contiguous on the host.
inline constexpr uint32_t kHostOffsetBoundary = 0xE0000000u;

}  // namespace torchlight::guest_abi::xbox_memory
