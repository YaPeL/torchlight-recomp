// SHA-256, for the digest at the end of an Xbox 360 save (third_party/sha256).

#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace torchlight::save_import {

using Sha256Digest = std::array<uint8_t, 32>;

Sha256Digest Sha256(std::span<const uint8_t> data);

}  // namespace torchlight::save_import
