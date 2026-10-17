// The system's Back button as the pad's Back (docs/android-port.md, stage A1.4; the owner's
// decision, 2026-10-17): Android's Back key (the navigation bar's, a phone's) held is the
// controller's Back held, through a controller of its own that the SDK gives guest user 0 together
// with any real pad. Only where the platform has such a button (platform::HasSystemBackButton:
// Android); elsewhere nothing is installed, so no controller appears where there is none.

#pragma once

namespace rex {
class Runtime;
}

namespace torchlight::live {

// Before the guest runs (with the other input drivers).
void InstallSystemBackButton(rex::Runtime* runtime);

}  // namespace torchlight::live
