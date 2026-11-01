// The guest's memory copies on the host (guest_copy_hooks.cpp, --native_guest_copy).

#pragma once

namespace torchlight::hooks {

// Logs whether the guest's copies run on the host (--native_guest_copy), once at startup.
void LogGuestCopyMode();
// Logs whether the guest's tree walks run on the host (guest_tree_hooks.cpp,
// --native_tree_lookups), once at startup.
void LogTreeLookupMode();

}  // namespace torchlight::hooks
