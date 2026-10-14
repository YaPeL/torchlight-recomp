// The host implementations of render system member writes (native_render_system.h), on a fake
// guest memory: the same bytes the guest's function stores, nothing else touched.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "guest_abi/ogre_enums.h"
#include "guest_abi/ogre_layout.h"
#include "guest_abi/rtss_listener.h"
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

  // The RTSS listener's lookup on a fake pass: a map<String, Any> of two nodes under a head node,
  // the SGPass held by an Any whose holder has the expected vtable.
  namespace rl = abi::rtss_listener;
  namespace tree = abi::ogre::stl_tree;
  namespace str = abi::ogre::stl_string;
  std::vector<uint8_t> g(0x10000, 0);
  auto W = [&](uint32_t a, uint32_t v) { abi::WriteU32(g.data(), a, v); };
  auto S = [&](uint32_t a, const char* text) {  // an MSVC std::string, inline
    for (uint32_t i = 0; text[i]; ++i) g[a + str::kBuffer.offset + i] = uint8_t(text[i]);
    W(a + str::kLength.offset, uint32_t(std::char_traits<char>::length(text)));
    W(a + str::kCapacity.offset, 15);
  };
  const uint32_t kPass = 0x1000, kAttrs = 0x2000, kMap = 0x2100, kHead = 0x2200, kA = 0x2300,
                 kB = 0x2400, kHolder = 0x2500, kSg = 0x2600, kKey = 0x2700;
  W(kPass + rl::kPassUserObjectBindings.offset + rl::kBindingsAttributes.offset, kAttrs);
  W(kAttrs + rl::kAttributesMap.offset, kMap);
  W(kMap + tree::kHead.offset, kHead);
  g[kHead + rl::kNodeIsNil.offset] = 1;
  W(kHead + tree::kNodeParent.offset, kB);  // root: "SGPass", with "Other" to its left
  W(kB + tree::kNodeLeft.offset, kA); W(kB + tree::kNodeRight.offset, kHead); W(kB + tree::kNodeParent.offset, kHead);
  W(kA + tree::kNodeLeft.offset, kHead); W(kA + tree::kNodeRight.offset, kHead); W(kA + tree::kNodeParent.offset, kB);
  S(kA + rl::kNodeKey.offset, "Other");
  S(kB + rl::kNodeKey.offset, "SGPass");
  S(kKey, "SGPass");
  W(kB + rl::kNodeAny.offset + rl::kAnyContent.offset, kHolder);
  W(kHolder, rl::kSgPassHolderVtable);
  W(kHolder + rl::kHolderHeld.offset, kSg);
  Check(FindStringKey(g.data(), kMap, "SGPass", 6) == kB && FindStringKey(g.data(), kMap, "Other", 5) == kA,
        "keys found in the tree");
  Check(FindStringKey(g.data(), kMap, "SGPas", 5) == 0 && FindStringKey(g.data(), kMap, "SGPasss", 7) == 0 &&
            FindStringKey(g.data(), kMap, "Zzz", 3) == 0,
        "prefixes, extensions and absent keys are not found");
  auto l = LookupSgPass(g.data(), kPass, kKey);
  Check(l.what == ListenerPass::kSgPass && l.sg_pass == kSg, "the SGPass is found through the Any");
  W(kHolder, 0x12345678);
  Check(LookupSgPass(g.data(), kPass, kKey).what == ListenerPass::kGuest, "another holder type is left to the guest");
  W(kHolder, rl::kSgPassHolderVtable);
  W(kB + rl::kNodeAny.offset + rl::kAnyContent.offset, 0);
  Check(LookupSgPass(g.data(), kPass, kKey).what == ListenerPass::kNothing, "an empty Any does nothing");
  W(kAttrs + rl::kAttributesMap.offset, 0);
  Check(LookupSgPass(g.data(), kPass, kKey).what == ListenerPass::kNothing, "no map does nothing");
  W(kPass + rl::kPassUserObjectBindings.offset + rl::kBindingsAttributes.offset, 0);
  Check(LookupSgPass(g.data(), kPass, kKey).what == ListenerPass::kGuest, "no attributes yet is left to the guest");

  if (failures) return 1;
  std::puts("native_render_system_test: ok");
  return 0;
}
