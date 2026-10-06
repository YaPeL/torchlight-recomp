// Minimal read-only zip archive (stored and deflate entries) with case-insensitive lookup.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace torchlight::frontend {

class ZipArchive {
 public:
  bool Open(const std::string& path, std::string& error);
  // Entry data by name, compared case-insensitively with '\\' and '/' treated alike.
  std::optional<std::vector<uint8_t>> Read(const std::string& name) const;
  size_t size() const { return entries_.size(); }
  // Every entry's name as stored in the archive.
  const std::vector<std::string>& names() const { return names_; }

 private:
  struct Entry {
    uint32_t method, compressed, uncompressed, local_offset;
  };
  static std::string Key(const std::string& name);
  std::vector<uint8_t> data_;
  std::unordered_map<std::string, Entry> entries_;
  std::vector<std::string> names_;
};

}  // namespace torchlight::frontend
