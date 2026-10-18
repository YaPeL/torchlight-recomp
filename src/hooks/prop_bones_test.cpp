// ShouldDeferPropBones / PropBonesCounts (hooks/prop_bones.h).

#include <cstdio>

#include "hooks/prop_bones.h"

namespace {
int g_failures = 0;
void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++g_failures;
  }
}
}  // namespace

int main() {
  using torchlight::hooks::PropBonesCounts;
  using torchlight::hooks::ShouldDeferPropBones;
  Check(ShouldDeferPropBones(true, true, 0), "a prop with nothing attached is deferred");
  Check(!ShouldDeferPropBones(false, true, 0), "never with the option off");
  Check(!ShouldDeferPropBones(true, false, 0), "never outside a layout prop's update (units)");
  Check(!ShouldDeferPropBones(true, true, 1), "never with an object attached to a bone");

  PropBonesCounts c;
  c.NewFrame(10);
  c.Deferred(0x1000, 0xA000);
  c.Deferred(0x2000, 0xB000);
  c.Evaluated();
  Check(c.BoneLookup(0xA000), "a lookup on a deferred skeleton is caught");
  Check(!c.BoneLookup(0xC000), "a lookup on another skeleton is not");
  c.RenderPath(0x1000, 0xA000);  // drawn: required
  Check(!c.BoneLookup(0xA000), "after the render path evaluated it, a lookup is fine");
  c.RenderPath(0x3000, 0xC000);  // never deferred: not counted
  c.RenderPath(0x1000, 0xA000);  // a second call in the frame: counted once
  Check(c.deferred() == 2 && c.evaluated() == 1 && c.required() == 1 &&
            c.read_while_deferred() == 1,
        "counts in one frame");
  c.NewFrame(11);
  c.RenderPath(0x2000, 0xB000);  // deferred in the previous frame only
  Check(!c.BoneLookup(0xB000), "a new frame forgets the deferred skeletons");
  Check(c.required() == 1, "a deferral is required only in its own frame");
  c.ResetTotals();
  Check(c.deferred() == 0 && c.required() == 0 && c.evaluated() == 0 &&
            c.read_while_deferred() == 0,
        "totals reset");
  return g_failures ? 1 : 0;
}
