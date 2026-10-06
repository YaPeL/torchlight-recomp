#include "save_import/digest.h"

#include <sha256.h>

namespace torchlight::save_import {

Sha256Digest Sha256(std::span<const uint8_t> data) {
  sha256::SHA256 hasher;
  hasher.add(data.data(), data.size());
  Sha256Digest digest{};
  hasher.getHash(digest.data());
  return digest;
}

}  // namespace torchlight::save_import
