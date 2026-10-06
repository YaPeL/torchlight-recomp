#include "png.h"

#include <fstream>

#include <zlib.h>

namespace torchlight::replay {

namespace {
void Be32(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back(uint8_t(x >> 24)); v.push_back(uint8_t(x >> 16));
  v.push_back(uint8_t(x >> 8)); v.push_back(uint8_t(x));
}
void Chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
  Be32(out, uint32_t(data.size()));
  size_t start = out.size();
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), data.begin(), data.end());
  Be32(out, uint32_t(crc32(0, &out[start], uInt(out.size() - start))));
}
}  // namespace

bool WritePng(const std::string& path, uint32_t width, uint32_t height,
              const std::vector<uint8_t>& rgba) {
  std::vector<uint8_t> raw;
  raw.reserve((width * 4 + 1) * height);
  for (uint32_t y = 0; y < height; ++y) {
    raw.push_back(0);
    raw.insert(raw.end(), rgba.begin() + y * width * 4, rgba.begin() + (y + 1) * width * 4);
  }
  uLongf size = compressBound(uLong(raw.size()));
  std::vector<uint8_t> z(size);
  if (compress2(z.data(), &size, raw.data(), uLong(raw.size()), 6) != Z_OK) return false;
  z.resize(size);
  std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  std::vector<uint8_t> ihdr;
  Be32(ihdr, width);
  Be32(ihdr, height);
  ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
  Chunk(out, "IHDR", ihdr);
  Chunk(out, "IDAT", z);
  Chunk(out, "IEND", {});
  std::ofstream f(path, std::ios::binary);
  f.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
  return bool(f);
}

}  // namespace torchlight::replay
