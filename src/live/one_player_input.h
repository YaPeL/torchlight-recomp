// Every controller plays the one player (issue #138, 2026-11-10). The SDK gives controller N to
// guest user N by default (SlotAssignment, since its July 2026 input rework); Torchlight is single
// player and only user 0 is signed in, so "Begin" pressed on a second controller (two pads
// connected, or a pad plus a virtual one such as Steam Input's) made the game ask user 1 for a
// gamer profile: "Sign In Error ... You must be signed in with a gamer profile", with no way past
// it. Every controller now feeds user 0 together (SharedAssignment), as the touch controls and the
// scripts' controller already did.

#pragma once

#include <memory>

namespace rex {
class Runtime;
namespace input {
class DeviceAssignment;
}  // namespace input
}  // namespace rex

namespace torchlight::live {

// The policy: every device, real or synthetic, feeds guest user 0; users 1-3 see none.
std::unique_ptr<rex::input::DeviceAssignment> OnePlayerAssignment();

// Before the guest runs (with the other input drivers).
void InstallOnePlayerInput(rex::Runtime* runtime);

}  // namespace torchlight::live
