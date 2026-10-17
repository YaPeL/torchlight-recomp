// The swap interval paces GL presents here (display_refresh.h): no clock of our own.

#include "platform/display_refresh.h"

namespace torchlight::platform {

std::unique_ptr<RefreshClock> CreateRefreshClock(const NativeWindow&) { return nullptr; }

}  // namespace torchlight::platform
