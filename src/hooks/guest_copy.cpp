#include "hooks/guest_copy.h"

#include <cstring>

#include <rex/system/xmemory.h>

#include "guest_abi/xbox_memory.h"

namespace torchlight::hooks {

namespace {

namespace mem = guest_abi::xbox_memory;

// guest_abi translates guest addresses with its own copy of the SDK's host offset (it builds
// without the SDK): both must agree on this platform.
static_assert(rex::memory::detail::PhysicalHostOffset(mem::kHostOffsetBoundary) ==
              mem::kHostOffset);
static_assert(rex::memory::detail::PhysicalHostOffset(mem::kHostOffsetBoundary - 1) == 0);

// Whether [begin, begin + size) (size > 0, inside the address space) lies on one side of the host
// offset boundary and outside the device registers.
bool Linear(uint64_t begin, uint64_t size) {
  const uint64_t end = begin + size;
  if (begin < mem::kHostOffsetBoundary && end > mem::kHostOffsetBoundary) return false;
  return end <= mem::kMmioBegin || begin >= mem::kMmioEnd;
}

}  // namespace

GuestCopyRoute RouteGuestCopy(uint32_t dst, uint32_t src, uint32_t size) {
  if (size == 0) return GuestCopyRoute::kNothing;
  constexpr uint64_t kSpace = uint64_t{1} << 32;
  if (uint64_t{dst} + size > kSpace || uint64_t{src} + size > kSpace) return GuestCopyRoute::kGuest;
  if (uint64_t{dst} < uint64_t{src} + size && uint64_t{src} < uint64_t{dst} + size)
    return GuestCopyRoute::kGuest;
  if (!Linear(dst, size) || !Linear(src, size)) return GuestCopyRoute::kGuest;
  return GuestCopyRoute::kHost;
}

bool HostGuestCopy(uint8_t* base, uint32_t dst, uint32_t src, uint32_t size) {
  switch (RouteGuestCopy(dst, src, size)) {
    case GuestCopyRoute::kNothing:
      return true;
    case GuestCopyRoute::kHost:
      std::memcpy(rex::memory::GuestPtr(base, dst), rex::memory::GuestPtr(base, src), size);
      return true;
    case GuestCopyRoute::kGuest:
      break;
  }
  return false;
}

}  // namespace torchlight::hooks
