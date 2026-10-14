#include "hooks/native_render_system.h"

#include "guest_abi/ogre_enums.h"
#include "guest_abi/ogre_layout.h"
#include "guest_abi/rtss_listener.h"
#include "guest_abi/xbox_memory.h"

#include <algorithm>
#include <cstring>

namespace torchlight::hooks {

namespace abi = torchlight::guest_abi;

bool NativeSetTextureBlendMode(uint8_t* membase, uint32_t render_system, uint32_t unit,
                               uint32_t blend_mode) {
  namespace lb = abi::ogre::layer_blend_mode_ex;
  namespace rs = abi::ogre::render_system;
  const uint32_t type = abi::ReadU32(membase, blend_mode, lb::kBlendType);
  if (type == abi::ogre::layer_blend_type::kAlpha.value) return true;  // reads only
  if (type != abi::ogre::layer_blend_type::kColour.value) return false;
  const uint32_t dst = render_system + rs::kManualBlendColours.offset +
                       unit * rs::kManualBlendColoursUnitStride;
  // Two ColourValues (four floats each), copied word by word in the guest's byte order.
  for (uint32_t i = 0; i < 4; ++i) {
    abi::WriteU32(membase, dst + 4 * i, abi::ReadU32(membase, blend_mode + lb::kColourArg1.offset + 4 * i));
    abi::WriteU32(membase, dst + 16 + 4 * i,
                  abi::ReadU32(membase, blend_mode + lb::kColourArg2.offset + 4 * i));
  }
  return true;
}

namespace {
// The guest std::string at `address` as a view into guest memory (MSVC: inline below 16 of capacity,
// else a pointer), with its length.
struct GuestStringView {
  const uint8_t* data = nullptr;
  uint32_t length = 0;
};
GuestStringView ViewGuestString(const uint8_t* membase, uint32_t address) {
  namespace str = abi::ogre::stl_string;
  const uint32_t length = abi::ReadU32(membase, address + str::kLength.offset);
  const uint32_t cap = abi::ReadU32(membase, address + str::kCapacity.offset);
  const uint32_t data = cap < 16 ? address + str::kBuffer.offset
                                 : abi::ReadU32(membase, address + str::kBuffer.offset);
  return {abi::xbox_memory::HostAddress(membase, data), length};
}
// std::string's compare of two views: bytes, then length.
int CompareViews(GuestStringView a, const uint8_t* b, uint32_t b_length) {
  const uint32_t n = std::min(a.length, b_length);
  const int c = n ? std::memcmp(a.data, b, n) : 0;
  if (c) return c;
  return a.length < b_length ? -1 : (a.length > b_length ? 1 : 0);
}
}  // namespace

uint32_t ReadGuestString(const uint8_t* membase, uint32_t address, char* out, uint32_t capacity) {
  const GuestStringView v = ViewGuestString(membase, address);
  std::memcpy(out, v.data, std::min(v.length, capacity));
  return v.length;
}

uint32_t FindStringKey(const uint8_t* membase, uint32_t map, const char* key, uint32_t key_length) {
  namespace tree = abi::ogre::stl_tree;
  namespace rl = abi::rtss_listener;
  const uint32_t head = abi::ReadU32(membase, map + tree::kHead.offset);
  if (!head) return 0;
  uint32_t node = abi::ReadU32(membase, head + tree::kNodeParent.offset);  // the root
  uint32_t candidate = 0;
  const auto* k = reinterpret_cast<const uint8_t*>(key);
  while (node && !abi::ReadU8(membase, node + rl::kNodeIsNil.offset)) {
    const int c = CompareViews(ViewGuestString(membase, node + rl::kNodeKey.offset), k, key_length);
    if (c < 0) {
      node = abi::ReadU32(membase, node + tree::kNodeRight.offset);
    } else {
      candidate = node;
      node = abi::ReadU32(membase, node + tree::kNodeLeft.offset);
    }
  }
  if (!candidate) return 0;
  return CompareViews(ViewGuestString(membase, candidate + rl::kNodeKey.offset), k, key_length) == 0 ? candidate : 0;
}

ListenerLookup LookupSgPass(const uint8_t* membase, uint32_t pass, uint32_t key_address) {
  namespace rl = abi::rtss_listener;
  const uint32_t bindings = pass + rl::kPassUserObjectBindings.offset;
  const uint32_t attributes = abi::ReadU32(membase, bindings + rl::kBindingsAttributes.offset);
  if (!attributes) return {ListenerPass::kGuest, 0};  // the guest allocates them here
  const uint32_t map = abi::ReadU32(membase, attributes + rl::kAttributesMap.offset);
  if (!map) return {ListenerPass::kNothing, 0};
  // The key is a static string the game fills at startup and never changes: read once it is set.
  static char key[64];
  static uint32_t key_length = 0;
  if (key_length == 0) {
    key_length = ReadGuestString(membase, key_address, key, sizeof key);
    if (key_length == 0 || key_length > sizeof key) {
      key_length = 0;
      return {ListenerPass::kGuest, 0};
    }
  }
  const uint32_t node = FindStringKey(membase, map, key, key_length);
  if (!node) return {ListenerPass::kNothing, 0};
  const uint32_t content = abi::ReadU32(membase, node + rl::kNodeAny.offset + rl::kAnyContent.offset);
  if (!content) return {ListenerPass::kNothing, 0};
  if (abi::ReadU32(membase, content) != rl::kSgPassHolderVtable) return {ListenerPass::kGuest, 0};
  const uint32_t sg_pass = abi::ReadU32(membase, content + rl::kHolderHeld.offset);
  return {sg_pass ? ListenerPass::kSgPass : ListenerPass::kNothing, sg_pass};
}

}  // namespace torchlight::hooks
