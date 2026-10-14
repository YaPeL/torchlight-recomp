// The host implementations of render system member writes (native_render_system.h), on a fake
// guest memory: the same bytes the guest's function stores, nothing else touched.

#include <cstdint>
#include <cstdio>
#include <vector>

#include "guest_abi/ogre_enums.h"
#include "guest_abi/ogre_layout.h"
#include "hooks/native_render_system.h"

namespace {

namespace abi = torchlight::guest_abi;
using namespace torchlight::hooks;

int failures = 0;
void Check(bool ok, const char* what) {
  if (ok) return;
  ++failures;
  std::fprintf(stderr, "FAIL: %s\n", what);
}

}  // namespace

int main() {
  namespace lb = abi::ogre::layer_blend_mode_ex;
  namespace rs = abi::ogre::render_system;
  const uint32_t kRs = 0x1000, kMode = 0x2000, kUnit = 3;
  std::vector<uint8_t> m(0x3000);
  for (size_t i = 0; i < m.size(); ++i) m[i] = uint8_t(i * 7 + 1);  // a pattern, so untouched bytes show
  for (uint32_t i = 0; i < 8; ++i) abi::WriteU32(m.data(), kMode + lb::kColourArg1.offset + 4 * i, 0xC0DE0000 + i);
  abi::WriteU32(m.data(), kMode, abi::ogre::layer_blend_type::kColour.value);
  std::vector<uint8_t> before = m;
  Check(NativeSetTextureBlendMode(m.data(), kRs, kUnit, kMode), "colour type handled");
  const uint32_t dst = kRs + rs::kManualBlendColours.offset + kUnit * rs::kManualBlendColoursUnitStride;
  bool stored = true;
  for (uint32_t i = 0; i < 8; ++i) stored = stored && abi::ReadU32(m.data(), dst + 4 * i) == 0xC0DE0000 + i;
  Check(stored, "colourArg1 and colourArg2 stored at mManualBlendColours[unit]");
  bool untouched = true;
  for (size_t i = 0; i < m.size(); ++i)
    if (i < dst || i >= dst + 32) untouched = untouched && m[i] == before[i];
  Check(untouched, "nothing else written");

  abi::WriteU32(m.data(), kMode, abi::ogre::layer_blend_type::kAlpha.value);
  before = m;
  Check(NativeSetTextureBlendMode(m.data(), kRs, kUnit, kMode) && m == before, "alpha type stores nothing");
  abi::WriteU32(m.data(), kMode, 7);
  before = m;
  Check(!NativeSetTextureBlendMode(m.data(), kRs, kUnit, kMode) && m == before,
        "another blend type is left to the guest");

  if (failures) return 1;
  std::puts("native_render_system_test: ok");
  return 0;
}
