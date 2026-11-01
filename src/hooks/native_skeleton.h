// OGRE's node and bone updates on the host (native_skeleton.cpp, --native_skeleton).

#pragma once

namespace torchlight::hooks {

// Logs whether OGRE's node and bone updates run on the host (--native_skeleton), once at startup.
void LogNativeSkeletonMode();

}  // namespace torchlight::hooks
