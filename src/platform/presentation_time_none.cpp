// No presentation times (presentation_time.h): every platform and build but Linux on Wayland with
// TORCHLIGHT_PRESENTATION_TIME.

#include "platform/presentation_time.h"

namespace torchlight::platform {

std::unique_ptr<PresentationClock> CreatePresentationClock(const NativeWindow&) { return nullptr; }

}  // namespace torchlight::platform
