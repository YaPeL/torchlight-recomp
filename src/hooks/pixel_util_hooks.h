// The guest OGRE's 8-bit colour pack and unpack on the host (hooks/pixel_util.h). Every mode;
// --native_pixel_util=false keeps the recompiled guest functions everywhere.

#pragma once

namespace torchlight::hooks {

// Logs which one runs, once at startup.
void LogPixelUtilMode();

// Diagnostics builds (TORCHLIGHT_DIAGNOSTICS) with --diag_wardrobe_composite: logs the digest of
// the pixels the wardrobe build just packed into the character's texture (its packColour call
// @0x822DD380), for comparing the same equipment with and without the host versions. Called by the
// wardrobe build's hook after the guest's build, with how long the build took; nothing in other
// builds.
void LogWardrobeComposite(double build_ms);

}  // namespace torchlight::hooks
