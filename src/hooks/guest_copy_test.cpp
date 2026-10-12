// RouteGuestCopy and HostGuestCopy: the edge cases of the guest's copies on the host.

#include <cstdint>
#include <cstdio>
#include <vector>

#include "guest_abi/xbox_memory.h"
#include "hooks/guest_copy.h"

namespace {

using torchlight::hooks::GuestCopyRoute;
using torchlight::hooks::HostGuestCopy;
using torchlight::hooks::RouteGuestCopy;
namespace mem = torchlight::guest_abi::xbox_memory;

int failures = 0;

void Check(bool ok, const char* what, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0) {
  if (ok) return;
  ++failures;
  std::fprintf(stderr, "FAIL: %s (0x%X, 0x%X, 0x%X)\n", what, a, b, c);
}

void TestRoutes() {
  // Size 0 touches nothing, wherever it points (even null or the registers).
  Check(RouteGuestCopy(0, 0, 0) == GuestCopyRoute::kNothing, "size 0");
  Check(RouteGuestCopy(mem::kMmioBegin, 0x40000000, 0) == GuestCopyRoute::kNothing, "size 0 mmio");
  // Plain copies, aligned or not.
  Check(RouteGuestCopy(0x40001000, 0x40002000, 0x100) == GuestCopyRoute::kHost, "aligned");
  Check(RouteGuestCopy(0x40001003, 0x40002005, 7) == GuestCopyRoute::kHost, "unaligned");
  // Adjacent ranges do not overlap; one shared byte does, in either direction.
  Check(RouteGuestCopy(0x40001000, 0x40001100, 0x100) == GuestCopyRoute::kHost, "adjacent below");
  Check(RouteGuestCopy(0x40001100, 0x40001000, 0x100) == GuestCopyRoute::kHost, "adjacent above");
  Check(RouteGuestCopy(0x40001000, 0x400010FF, 0x100) == GuestCopyRoute::kGuest, "overlap dst<src");
  Check(RouteGuestCopy(0x400010FF, 0x40001000, 0x100) == GuestCopyRoute::kGuest, "overlap dst>src");
  Check(RouteGuestCopy(0x40001000, 0x40001000, 1) == GuestCopyRoute::kGuest, "same range");
  // Device registers: touching the range from either side, or inside it.
  Check(RouteGuestCopy(mem::kMmioBegin - 0x10, 0x40000000, 0x10) == GuestCopyRoute::kHost,
        "ends at mmio");
  Check(RouteGuestCopy(mem::kMmioBegin - 0x10, 0x40000000, 0x11) == GuestCopyRoute::kGuest,
        "enters mmio");
  Check(RouteGuestCopy(0x40000000, 0x7FC80000, 4) == GuestCopyRoute::kGuest, "src in mmio");
  Check(RouteGuestCopy(mem::kMmioEnd - 1, 0x40000000, 1) == GuestCopyRoute::kGuest, "last mmio");
  Check(RouteGuestCopy(mem::kMmioEnd, 0x40000000, 0x10) == GuestCopyRoute::kHost, "after mmio");
  // The host offset boundary: up to it, from it, across it.
  const uint32_t e = mem::kHostOffsetBoundary;
  Check(RouteGuestCopy(e - 0x10, 0x40000000, 0x10) == GuestCopyRoute::kHost, "ends at boundary");
  Check(RouteGuestCopy(e, 0x40000000, 0x10) == GuestCopyRoute::kHost, "from boundary");
  Check(RouteGuestCopy(e - 0x10, 0x40000000, 0x11) == GuestCopyRoute::kGuest, "dst crosses");
  Check(RouteGuestCopy(0x40000000, e - 1, 2) == GuestCopyRoute::kGuest, "src crosses");
  // The end of the address space: the last byte is fine, past it is not.
  Check(RouteGuestCopy(0xFFFFFFF0, 0x40000000, 0x10) == GuestCopyRoute::kHost, "last bytes");
  Check(RouteGuestCopy(0xFFFFFFF0, 0x40000000, 0x11) == GuestCopyRoute::kGuest, "dst wraps");
  Check(RouteGuestCopy(0x40000000, 0xFFFFFFFF, 2) == GuestCopyRoute::kGuest, "src wraps");
  Check(RouteGuestCopy(0x40000000, 0x50000000, 0xFFFFFFFF) == GuestCopyRoute::kGuest, "huge");
}

// The guest's result between ranges that do not overlap: every byte of src lands at dst, front to
// back (any copy order gives the same bytes then).
void ReferenceCopy(std::vector<uint8_t>& m, uint32_t dst, uint32_t src, uint32_t size) {
  for (uint32_t i = 0; i < size; ++i) m[dst + i] = m[src + i];
}

std::vector<uint8_t> Pattern(size_t size) {
  std::vector<uint8_t> m(size);
  for (size_t i = 0; i < size; ++i) m[i] = uint8_t(i * 131 + 7);
  return m;
}

void TestCopies() {
  // A small fake address space: low addresses translate with no offset on every host.
  constexpr size_t kSpace = 0x2000;
  // Every alignment of destination and source (mod 16) and sizes around the guest's paths (byte,
  // word, doubleword, 128-byte blocks; the large copy's 256-byte threshold).
  for (uint32_t size : {0u, 1u, 3u, 4u, 7u, 8u, 9u, 15u, 16u, 127u, 128u, 129u, 255u, 256u, 1025u}) {
    for (uint32_t da = 0; da < 16; ++da) {
      for (uint32_t sa = 0; sa < 16; ++sa) {
        const uint32_t dst = 0x100 + da, src = 0x900 + sa;
        std::vector<uint8_t> host = Pattern(kSpace), want = Pattern(kSpace);
        const bool copied = HostGuestCopy(host.data(), dst, src, size);
        ReferenceCopy(want, dst, src, size);
        Check(copied, "copied", dst, src, size);
        Check(host == want, "bytes", dst, src, size);
      }
    }
  }
  // Size 0 at address 0: nothing is read or written (the base itself is not touched).
  std::vector<uint8_t> m = Pattern(16), before = m;
  Check(HostGuestCopy(m.data(), 0, 8, 0) && m == before, "size 0 at 0");
  // Overlapping ranges are left to the guest, untouched.
  for (uint32_t delta : {1u, 7u, 8u, 64u, 255u}) {
    std::vector<uint8_t> o = Pattern(kSpace), o_before = o;
    Check(!HostGuestCopy(o.data(), 0x100 + delta, 0x100, 256) && o == o_before, "overlap up", delta);
    Check(!HostGuestCopy(o.data(), 0x100, 0x100 + delta, 256) && o == o_before, "overlap down",
          delta);
  }
}

}  // namespace

int main() {
  TestRoutes();
  TestCopies();
  if (failures) {
    std::fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
  }
  std::puts("guest_copy_test: ok");
  return 0;
}
