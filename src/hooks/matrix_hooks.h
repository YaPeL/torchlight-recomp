#pragma once

namespace torchlight::hooks {

// Reads --native_matrix_concatenate and logs the mode (before the guest runs).
void InstallMatrixConcatenate();

}  // namespace torchlight::hooks
