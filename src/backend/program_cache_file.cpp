#include "backend/program_cache_file.h"

#include <cstdint>
#include <cstring>

namespace torchlight::backend {

namespace {

constexpr char kMagic[8] = {'t', 'l', 'p', 'c', 'a', 'c', 'h', '1'};
constexpr size_t kTrailer = sizeof(kMagic) + 8 + 8;  // magic, payload length, checksum

uint64_t Checksum(const char* data, size_t size) {
  // FNV-1a, 64 bits: catches a cut or a damaged byte, not tampering.
  uint64_t hash = 14695981039346656037ull;
  for (size_t i = 0; i < size; ++i) {
    hash ^= uint8_t(data[i]);
    hash *= 1099511628211ull;
  }
  return hash;
}

void PutU64(std::string& out, uint64_t value) {
  for (int i = 0; i < 8; ++i) out += char(value >> (8 * i));
}

uint64_t GetU64(const char* in) {
  uint64_t value = 0;
  for (int i = 0; i < 8; ++i) value |= uint64_t(uint8_t(in[i])) << (8 * i);
  return value;
}

}  // namespace

std::string WrapProgramCache(const std::string& payload) {
  std::string out = payload;
  out.append(kMagic, sizeof(kMagic));
  PutU64(out, payload.size());
  PutU64(out, Checksum(payload.data(), payload.size()));
  return out;
}

std::optional<std::string> UnwrapProgramCache(const std::string& file) {
  if (file.size() < kTrailer) return std::nullopt;
  const size_t size = file.size() - kTrailer;
  const char* trailer = file.data() + size;
  if (std::memcmp(trailer, kMagic, sizeof(kMagic)) != 0) return std::nullopt;
  if (GetU64(trailer + sizeof(kMagic)) != size) return std::nullopt;
  if (GetU64(trailer + sizeof(kMagic) + 8) != Checksum(file.data(), size)) return std::nullopt;
  return file.substr(0, size);
}

}  // namespace torchlight::backend
