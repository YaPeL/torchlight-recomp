// The busy notice's line (busy_notice.h), apart from ImGui so it can be tested.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace torchlight::live {

// The notice's texts in English, translated in data/ui/tl_import_strings.txt.
// The mods' unit index being built (mods/unit_index_install.cpp).
inline constexpr const char* kPreparingModsText = "Preparing mods... {percent}%";

// The share of `done` in `total` as a whole percent, rounded down so 100 only shows at the end; 0
// without a total.
int BusyPercent(size_t done, size_t total);
// `text` (translated) with every "{percent}" replaced by BusyPercent.
std::string BusyNoticeLine(std::string_view text, size_t done, size_t total);

}  // namespace torchlight::live
