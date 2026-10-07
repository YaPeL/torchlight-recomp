// The guest's D3D work skipped in the native mode (--native_skip_guest_d3d,
// docs/native-skip-guest-d3d.md).
//
// In the native mode the RenderSystem hooks record what the backend needs from the call's
// arguments and then run the guest's implementation, which programs the Xbox D3D device for a
// Xenos GPU the null plugin drops. For the slots below that implementation does nothing else (the
// evidence, store by store, is in the document), so the hook can return after recording. Xenos and
// parallel draw with that device: there nothing is skipped.

#pragma once

#include <cstdint>

namespace torchlight::hooks {

// RenderSystem slots whose guest implementation only programs the device ("Step a: evidence"):
// 44 _setTextureUnitFiltering (one filter), 46 _setTextureLayerAnisotropy,
// 47 _setTextureAddressingMode, 49 _setTextureMipmapBias. Not 45 (its three calls to 44 are what
// our slot 44 hook records) nor 43 (it writes render system members).
constexpr bool SkippableSlot(uint32_t slot) {
  switch (slot) {
    case 44:
    case 46:
    case 47:
    case 49:
      return true;
    default:
      return false;
  }
}

// Whether the hooks skip those implementations: the native mode only, and the cvar on.
constexpr bool SkipGuestD3D(bool native_only, bool enabled) { return native_only && enabled; }

// Before the guest runs: decides once for the session and logs it.
void InstallGuestD3DSkip(bool native_only);

}  // namespace torchlight::hooks
