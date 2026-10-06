#include "save_import/inflate.h"

#include <miniz.h>

namespace torchlight::save_import {

bool Inflate(std::span<const uint8_t> compressed, size_t size, std::vector<uint8_t>& out) {
  // One byte more than expected, so a stream that is longer than its stated size is detected.
  out.assign(size + 1, 0);
  const size_t written =
      tinfl_decompress_mem_to_mem(out.data(), out.size(), compressed.data(), compressed.size(), 0);
  if (written == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED || written != size) {
    out.clear();
    return false;
  }
  out.resize(size);
  return true;
}

}  // namespace torchlight::save_import
