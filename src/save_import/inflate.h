// Raw DEFLATE, the compression of the game paks' entries (zip method 8), through miniz.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace torchlight::save_import {

// Inflates `compressed` into `out`, which must come out exactly `size` bytes long (the size the
// zip directory states). False, with `out` cleared, if the stream is damaged or the size differs.
bool Inflate(std::span<const uint8_t> compressed, size_t size, std::vector<uint8_t>& out);

}  // namespace torchlight::save_import
