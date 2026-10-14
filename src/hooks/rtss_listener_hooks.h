// The RTSS per-object listener as host code (rtss_listener_hooks.cpp; --native_rtss_listener).
#pragma once

namespace torchlight::hooks {

// Before the guest runs: decides for the session (native mode and the cvar) and logs it.
void InstallRtssListener(bool native_only);

}  // namespace torchlight::hooks
