// RGBA8 PNG writer (zlib).

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace torchlight::replay {

bool WritePng(const std::string& path, uint32_t width, uint32_t height,
              const std::vector<uint8_t>& rgba);

}  // namespace torchlight::replay
