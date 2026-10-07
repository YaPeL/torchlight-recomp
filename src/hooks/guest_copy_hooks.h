// The guest's memory copies on the host (guest_copy_hooks.cpp, --native_guest_copy).

#pragma once

namespace torchlight::hooks {

// Logs whether the guest's copies run on the host (--native_guest_copy), once at startup.
void LogGuestCopyMode();

}  // namespace torchlight::hooks
