// Tests for the frontend's guest constant store: unwritten indices read as missing, writes read
// back, near and far indices alike, and growing keeps what was written.

#include <cstdio>
#include <cstdlib>

#include "frontend/physical_constants.h"

namespace {

using torchlight::frontend::PhysicalConstants;

void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    std::exit(1);
  }
}

}  // namespace

int main() {
  PhysicalConstants c;
  Check(!c.Get(0) && !c.Get(1000) && !c.Get(PhysicalConstants::kDense + 5), "empty");
  c.Set(3, 1.5f);
  Check(c.Get(3) && *c.Get(3) == 1.5f, "written index reads back");
  Check(!c.Get(2) && !c.Get(4), "neighbours stay unwritten");
  c.Set(900, -2.0f);  // grows the array
  Check(c.Get(3) && *c.Get(3) == 1.5f, "growing keeps earlier writes");
  Check(c.Get(900) && *c.Get(900) == -2.0f, "grown index reads back");
  Check(!c.Get(899) && !c.Get(1799), "grown indices stay unwritten");
  c.Set(PhysicalConstants::kDense - 1, 7.0f);
  Check(c.Get(PhysicalConstants::kDense - 1) && *c.Get(PhysicalConstants::kDense - 1) == 7.0f,
        "last dense index");
  c.Set(0xFFFFFFF0u, 9.0f);
  Check(c.Get(0xFFFFFFF0u) && *c.Get(0xFFFFFFF0u) == 9.0f, "far index reads back");
  Check(!c.Get(0xFFFFFFF1u), "other far index unwritten");
  const float range[3] = {10, 11, 12};
  c.SetRange(PhysicalConstants::kDense - 1, range, 3);  // straddles the dense end
  Check(*c.Get(PhysicalConstants::kDense - 1) == 10 && *c.Get(PhysicalConstants::kDense) == 11 &&
            *c.Get(PhysicalConstants::kDense + 1) == 12,
        "a range across the dense end");
  c.SetRange(0xFFFFFFFEu, range, 3);  // past the last index: wraps to 0
  Check(*c.Get(0xFFFFFFFEu) == 10 && *c.Get(0xFFFFFFFFu) == 11 && c.Get(0) && *c.Get(0) == 12,
        "a range at the top of the index space wraps");
  c.Set(3, 4.0f);
  Check(*c.Get(3) == 4.0f, "rewrite");
  std::printf("physical_constants_test: ok\n");
  return 0;
}
