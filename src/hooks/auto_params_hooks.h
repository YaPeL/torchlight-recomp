// GpuProgramParameters::_updateAutoParams replayed as host code (auto_params_hooks.cpp;
// --native_auto_params).
#pragma once

namespace torchlight::hooks {

// Before the guest runs: decides for the session (native mode and the cvar) and logs it.
void InstallAutoParams(bool native_only);

}  // namespace torchlight::hooks
