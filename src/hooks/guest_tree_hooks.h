// The guest's hottest tree walks on the host (guest_tree_hooks.cpp, --native_tree_lookups).

#pragma once

namespace torchlight::hooks {

// Logs whether the guest's tree walks run on the host (--native_tree_lookups), once at startup.
void LogTreeLookupMode();
// Logs whether OGRE's node and bone updates run on the host (native_skeleton.cpp,
// --native_skeleton), once at startup.
void LogNativeSkeletonMode();

}  // namespace torchlight::hooks
