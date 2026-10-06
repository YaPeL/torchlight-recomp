#include "frontend/zip_archive.h"

#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>

#include <zlib.h>

namespace torchlight::frontend {

namespace {
uint16_t U16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t U32(const uint8_t* p) { return uint32_t(p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24); }
}  // namespace

std::string ZipArchive::Key(const std::string& name) {
  std::string k;
  for (char c : name) k.push_back(c == '\\' ? '/' : char(std::tolower(uint8_t(c))));
  return k;
}

bool ZipArchive::Open(const std::string& path, std::string& error) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    error = "cannot open " + path;
    return false;
  }
  data_.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  // End of central directory: last 0x06054b50 within the final 64 KiB + 22 bytes.
  size_t n = data_.size();
  size_t eocd = std::string::npos;
  for (size_t i = n >= 22 ? n - 22 : 0; i + 22 <= n && n - i <= 65557; --i) {
    if (U32(&data_[i]) == 0x06054b50) {
      eocd = i;
      break;
    }
    if (i == 0) break;
  }
  if (eocd == std::string::npos) {
    error = "not a zip archive: " + path;
    return false;
  }
  uint32_t count = U16(&data_[eocd + 10]);
  size_t p = U32(&data_[eocd + 16]);
  for (uint32_t i = 0; i < count && p + 46 <= n; ++i) {
    if (U32(&data_[p]) != 0x02014b50) break;
    Entry e{U16(&data_[p + 10]), U32(&data_[p + 20]), U32(&data_[p + 24]), U32(&data_[p + 42])};
    uint16_t name_len = U16(&data_[p + 28]), extra = U16(&data_[p + 30]),
             comment = U16(&data_[p + 32]);
    std::string name(reinterpret_cast<const char*>(&data_[p + 46]), name_len);
    entries_[Key(name)] = e;
    names_.push_back(name);
    p += 46 + name_len + extra + comment;
  }
  return true;
}

std::optional<std::vector<uint8_t>> ZipArchive::Read(const std::string& name) const {
  auto it = entries_.find(Key(name));
  if (it == entries_.end()) return std::nullopt;
  const Entry& e = it->second;
  size_t p = e.local_offset;
  if (p + 30 > data_.size() || U32(&data_[p]) != 0x04034b50) return std::nullopt;
  size_t start = p + 30 + U16(&data_[p + 26]) + U16(&data_[p + 28]);
  if (start + e.compressed > data_.size()) return std::nullopt;
  if (e.method == 0) return std::vector<uint8_t>(&data_[start], &data_[start] + e.compressed);
  if (e.method != 8) return std::nullopt;
  std::vector<uint8_t> out(e.uncompressed);
  z_stream z{};
  z.next_in = const_cast<Bytef*>(&data_[start]);
  z.avail_in = e.compressed;
  z.next_out = out.data();
  z.avail_out = uInt(out.size());
  if (inflateInit2(&z, -MAX_WBITS) != Z_OK) return std::nullopt;
  int r = inflate(&z, Z_FINISH);
  inflateEnd(&z);
  if (r != Z_STREAM_END) return std::nullopt;
  return out;
}

}  // namespace torchlight::frontend
