// For the tests: a small, valid STFS package of the game's kind holding invented files, as the
// runtime's reader takes it (read-only format, one hash table, below 170 blocks). Nothing of the game.
// Layouts: rex/filesystem/devices/stfs_xbox.h.

#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace torchlight::game_setup {

inline bool WriteSyntheticPackage(const std::filesystem::path& path,
                                  const std::vector<std::pair<std::string, std::string>>& files) {
  constexpr size_t kBlock = 0x1000, kHeader = 0xA000;
  constexpr uint32_t kEndOfChain = 0xFFFFFF;
  size_t blocks = 1;  // the file table
  for (const auto& [name, data] : files) blocks += std::max<size_t>(1, (data.size() + kBlock - 1) / kBlock);
  if (blocks >= 170 || files.size() > 64) return false;

  std::string out(kHeader + kBlock * (blocks + 1), '\0');
  const auto be32 = [&](size_t at, uint32_t value) {
    for (int i = 0; i < 4; ++i) out[at + i] = char(value >> (24 - i * 8));
  };
  const auto le24 = [&](size_t at, uint32_t value) {
    for (int i = 0; i < 3; ++i) out[at + i] = char(value >> (i * 8));
  };
  out.replace(0, 4, "LIVE");
  be32(0x340, 0x971A);       // the header's size
  be32(0x344, 0x000D0000);   // content type: Arcade Title
  be32(0x360, 0x58410A7E);   // title ID
  out[0x379] = 0x24;         // the volume descriptor's length
  out[0x37B] = 1;            // read-only format
  out[0x37C] = 1;            // file table blocks (little endian), at block 0
  be32(0x395, uint32_t(blocks));

  const auto block_at = [&](size_t block) { return kHeader + kBlock * (block + 1); };
  const auto chain = [&](size_t block, bool last) {
    be32(kHeader + block * 0x18 + 0x14, 0x80000000u | (last ? kEndOfChain : uint32_t(block + 1)));
  };
  chain(0, true);
  size_t next = 1, index = 0;
  for (const auto& [name, data] : files) {
    const size_t count = std::max<size_t>(1, (data.size() + kBlock - 1) / kBlock);
    const size_t entry = block_at(0) + index++ * 0x40;
    out.replace(entry, std::min<size_t>(name.size(), 40), name);
    out[entry + 0x28] = char(name.size());
    le24(entry + 0x29, uint32_t(count));
    le24(entry + 0x2C, uint32_t(count));
    le24(entry + 0x2F, uint32_t(next));
    out[entry + 0x32] = char(0xFF);  // in the root
    out[entry + 0x33] = char(0xFF);
    be32(entry + 0x34, uint32_t(data.size()));
    for (size_t i = 0; i < count; ++i) chain(next + i, i + 1 == count);
    out.replace(block_at(next), data.size(), data);
    next += count;
  }
  std::ofstream file(path, std::ios::binary);
  file.write(out.data(), std::streamsize(out.size()));
  file.close();
  return bool(file);
}

}  // namespace torchlight::game_setup
