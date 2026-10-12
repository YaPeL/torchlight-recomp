// The guest's memory copies on the host (guest_abi functions::kMemcpy, kLargeCopy).
//
// The guest copies front to back with plain loads and stores, so between ranges that do not
// overlap it is a host memcpy. Everything else runs the guest's own copy, unchanged:
// - overlapping ranges: a front-to-back copy and memmove differ there (and the guest's loops move
//   8 to 1024 bytes at a time, which a byte model would not match either);
// - a range touching the device registers (guest_abi xbox_memory::kMmio*): the SDK serves those
//   accesses by decoding each faulting instruction, which only knows simple moves;
// - a range crossing the host offset boundary or the end of the address space: one host pointer
//   would not cover it.
// Pages the SDK watches (write-protected to invalidate GPU copies) need nothing: its fault handler
// unprotects and retries any host instruction, the guest's own stores the same way.

#pragma once

#include <cstdint>

namespace torchlight::hooks {

enum class GuestCopyRoute : uint8_t {
  kNothing,  // size 0: neither copy touches memory
  kHost,     // a host memcpy of the translated ranges
  kGuest,    // the guest's own copy
};

GuestCopyRoute RouteGuestCopy(uint32_t dst, uint32_t src, uint32_t size);

// Copies size bytes from guest src to guest dst on the host, translated as the generated code does
// (rex::memory::GuestPtr), when RouteGuestCopy allows; returns false when the guest must copy.
bool HostGuestCopy(uint8_t* base, uint32_t dst, uint32_t src, uint32_t size);

}  // namespace torchlight::hooks
