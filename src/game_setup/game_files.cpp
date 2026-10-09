#include "game_setup/game_files.h"

#include <array>
#include <cstdio>
#include <fstream>
#include <vector>

#include <sha256.h>

namespace torchlight::game_setup {

namespace {

constexpr GameFile kGameFiles[] = {
#include "game_setup/game_files_table.inc"
};

std::string Hex(uint32_t value) {
  char text[16];
  std::snprintf(text, sizeof(text), "0x%08X", value);
  return text;
}

}  // namespace

std::span<const GameFile> GameFiles() { return kGameFiles; }

Message CheckPackage(const PackageFacts& facts) {
  if (!facts.magic_valid) return {std::string(kTextNotPackage), {}};
  if (facts.content_type != kContentTypeArcadeTitle) {
    return {std::string(kTextNotArcade), {{"value", Hex(facts.content_type)}}};
  }
  if (facts.title_id != kTitleId) {
    return {std::string(kTextNotTorchlight),
            {{"value", Hex(facts.title_id)}, {"expected", Hex(kTitleId)}}};
  }
  return {};
}

std::string Sha256File(const std::filesystem::path& file, const Progress& progress,
                       uint64_t* done, uint64_t total) {
  std::ifstream in(file, std::ios::binary);
  if (!in) return "";
  sha256::SHA256 hash;
  std::vector<char> buffer(1 << 20);
  while (in) {
    in.read(buffer.data(), std::streamsize(buffer.size()));
    const std::streamsize got = in.gcount();
    if (got <= 0) break;
    hash.add(buffer.data(), size_t(got));
    if (done) *done += uint64_t(got);
    if (progress && !progress(done ? *done : 0, total)) return "";
  }
  if (in.bad()) return "";
  return hash.getHash();
}

namespace {

// Every file there with its size; else the message for the first that is not.
Message CheckSizes(const std::filesystem::path& dir, std::span<const GameFile> files) {
  std::error_code ec;
  for (const GameFile& f : files) {
    const std::filesystem::path path = dir / std::filesystem::path(std::string(f.path));
    if (!std::filesystem::is_regular_file(path, ec)) {
      return {std::string(kTextMissing), {{"file", std::string(f.path)}}};
    }
    if (std::filesystem::file_size(path, ec) != f.size || ec) {
      return {std::string(kTextOtherSize), {{"file", std::string(f.path)}}};
    }
  }
  return {};
}

}  // namespace

Message VerifyGameFolder(const std::filesystem::path& dir, std::span<const GameFile> files,
                         const Progress& progress) {
  uint64_t total = 0, done = 0;
  for (const GameFile& f : files) total += f.size;
  // Sizes first: a missing or truncated file is found without hashing the rest.
  if (Message sizes = CheckSizes(dir, files); !sizes.empty()) return sizes;
  for (const GameFile& f : files) {
    const std::filesystem::path path = dir / std::filesystem::path(std::string(f.path));
    const std::string digest = Sha256File(path, progress, &done, total);
    if (digest.empty()) {
      if (progress && !progress(done, total)) return {std::string(kCancelled), {}};
      return {std::string(kTextCannotRead), {{"file", std::string(f.path)}}};
    }
    if (digest != f.sha256) {
      return {std::string(kTextDiffers), {{"file", std::string(f.path)}}};
    }
  }
  return {};
}

bool XexMatches(const std::filesystem::path& dir, std::span<const GameFile> files) {
  for (const GameFile& f : files) {
    if (f.path != kXexPath) continue;
    const std::filesystem::path path = dir / std::string(kXexPath);
    std::error_code ec;
    if (std::filesystem::file_size(path, ec) != f.size || ec) return false;
    return Sha256File(path) == f.sha256;
  }
  return false;
}

Message QuickCheckGameFolder(const std::filesystem::path& dir, std::span<const GameFile> files) {
  if (Message sizes = CheckSizes(dir, files); !sizes.empty()) return sizes;
  if (!XexMatches(dir, files)) return {std::string(kTextDiffers), {{"file", std::string(kXexPath)}}};
  return {};
}

}  // namespace torchlight::game_setup
