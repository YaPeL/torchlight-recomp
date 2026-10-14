#pragma once

namespace torchlight::hooks {

// Reads --native_frustum_test and logs the mode (before the guest runs).
void InstallFrustumTest();

}  // namespace torchlight::hooks
