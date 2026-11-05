// The guest OGRE's 8-bit colour pack and unpack on the host (hooks/pixel_util.h). Every mode;
// --native_pixel_util=false keeps the recompiled guest functions everywhere.

#pragma once

#include <cstdint>

#include "hooks/pixel_util.h"

namespace torchlight::hooks {

// --native_pixel_util.
bool NativePixelUtilEnabled();

// The guest's description of a pixel format, from its table in guest memory (parsed once and
// compared at every call, as the hooks do); false for an index outside the table.
bool GuestPixelFormat(uint8_t* base, uint32_t format, PixelFormatDesc& desc);

// Diagnostics builds, --diag_wardrobe_composite: one pixel of the wardrobe build's loop written into
// the base at dest, by the host loop (hooks/wardrobe_blend_hooks.cpp), counted as the guest's pack
// at 0x822DD380 would be. Nothing in other builds.
void RecordWardrobeCompositePixel(uint8_t* base, uint32_t format, uint32_t dest);
// Whether --diag_wardrobe_composite records (false in other builds).
bool WardrobeCompositeRecording();

// Logs which one runs, once at startup.
void LogPixelUtilMode();

// Diagnostics builds (TORCHLIGHT_DIAGNOSTICS) with --diag_wardrobe_composite: logs the digest of
// the pixels the wardrobe build just packed into the character's texture (its packColour call
// @0x822DD380), for comparing the same equipment with and without the host versions. Called by the
// wardrobe build's hook after the guest's build; nothing in other builds.
void LogWardrobeComposite();

}  // namespace torchlight::hooks
