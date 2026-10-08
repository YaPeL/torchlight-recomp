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

// Host translation: base + address, plus kHostOffset from this address up (REX_PHYS_HOST_OFFSET in
// generated/default/torchlight_pch.h, rex::memory::GuestPtr). A range on one side of it is
// contiguous on the host.
inline constexpr uint32_t kHostOffsetBoundary = 0xE0000000u;

// 0x1000 where the host's allocation granularity is coarser than the guest's 4 KB pages (Windows,
// macOS arm64: the SDK maps the 0xE0000000 heap 0x1000 further up), 0 elsewhere (Linux). The build
// sets it (cmake/guest_host_offset.cmake); hooks/guest_copy.cpp checks it against the SDK's
// rex::memory::detail::PhysicalHostOffset at compile time.
inline constexpr uint32_t kHostOffset = TORCHLIGHT_GUEST_HOST_OFFSET;

// Distance in bytes from the host base (the host address of guest 0) to guest `address`.
constexpr uint64_t HostDistance(uint32_t address, uint32_t host_offset = kHostOffset) {
  return uint64_t{address} + (address >= kHostOffsetBoundary ? host_offset : 0u);
}

// The host address of guest `address`. Every access to guest memory goes through this (or the
// readers in ogre_layout.h, which use it); a range is translated by its start (see above).
inline uint8_t* HostAddress(uint8_t* base, uint32_t address) {
  return base + HostDistance(address);
}
inline const uint8_t* HostAddress(const uint8_t* base, uint32_t address) {
  return base + HostDistance(address);
}

}  // namespace torchlight::guest_abi::xbox_memory
