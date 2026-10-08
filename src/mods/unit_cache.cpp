#include "mods/unit_cache.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <system_error>

namespace torchlight::mods {

namespace fs = std::filesystem;

namespace {

constexpr uint64_t kFnvBasis = 0xCBF29CE484222325ull;
constexpr uint64_t kFnvPrime = 0x100000001B3ull;

uint64_t Fnv(uint64_t h, const void* data, size_t size) {
  const auto* p = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < size; ++i) {
    h ^= p[i];
    h *= kFnvPrime;
  }
  return h;
}

std::optional<std::vector<uint8_t>> ReadAll(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
}

std::string Hex(uint64_t v) {
  char buf[17];
  std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(v));
  return buf;
}

// "media/units/items/x.dat" (relative, generic separators) -> u"MEDIA/UNITS/ITEMS/X.DAT".
std::u16string GamePath(const fs::path& relative) {
  std::u16string out;
  for (char c : relative.generic_string()) {
    out.push_back(static_cast<char16_t>(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : static_cast<unsigned char>(c)));
  }
  return out;
}

bool IsUnitDefinition(const std::u16string& game_path) {
  static constexpr std::u16string_view kPrefix = u"MEDIA/UNITS/";
  static constexpr std::u16string_view kSuffix = u".DAT";
  return game_path.size() > kPrefix.size() + kSuffix.size() &&
         game_path.compare(0, kPrefix.size(), kPrefix) == 0 &&
         game_path.compare(game_path.size() - kSuffix.size(), kSuffix.size(), kSuffix) == 0;
}

}  // namespace

std::vector<ModUnitFile> ScanModUnitFiles(const fs::path& mods_folder, const ModPlan& plan) {
  std::vector<ModUnitFile> files;
  for (const PlannedMod& mod : plan.mods) {
    if (mod.priority < 0) continue;
    const fs::path root = mods_folder / mod.folder;
    std::error_code ec;
    std::vector<ModUnitFile> found;
    for (auto it = fs::recursive_directory_iterator(root, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
      std::error_code type_ec;
      if (!it->is_regular_file(type_ec)) continue;
      const std::u16string game_path = GamePath(fs::relative(it->path(), root, type_ec));
      if (type_ec || !IsUnitDefinition(game_path)) continue;
      const auto bytes = ReadAll(it->path());
      if (!bytes) continue;
      found.push_back({mod.folder, game_path, Fnv(kFnvBasis, bytes->data(), bytes->size())});
    }
    std::sort(found.begin(), found.end(),
              [](const ModUnitFile& a, const ModUnitFile& b) { return a.game_path < b.game_path; });
    files.insert(files.end(), found.begin(), found.end());
  }
  return files;
}

std::vector<std::u16string> UnitPathsByPriority(const std::vector<ModUnitFile>& files) {
  // A path's rank is the position of the last (highest priority) file that has it.
  std::map<std::u16string, size_t> rank;
  for (size_t i = 0; i < files.size(); ++i) rank[files[i].game_path] = i;
  std::vector<std::pair<size_t, std::u16string>> ordered;
  for (const auto& [path, r] : rank) ordered.emplace_back(r, path);
  std::sort(ordered.begin(), ordered.end());
  std::vector<std::u16string> out;
  for (auto& [r, path] : ordered) out.push_back(std::move(path));
  return out;
}

std::optional<uint64_t> PakIdentity(const fs::path& pak) {
  std::ifstream in(pak, std::ios::binary | std::ios::ate);
  if (!in) return std::nullopt;
  const std::streamoff size = in.tellg();
  if (size < 22) return std::nullopt;
  // The end of central directory record: within the last 22 + 65535 bytes.
  const std::streamoff tail_size = std::min<std::streamoff>(size, 22 + 65535);
  std::vector<uint8_t> tail(static_cast<size_t>(tail_size));
  in.seekg(size - tail_size);
  if (!in.read(reinterpret_cast<char*>(tail.data()), tail_size)) return std::nullopt;
  auto u32 = [](const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; };
  for (size_t i = tail.size() - 22 + 1; i-- > 0;) {
    if (u32(&tail[i]) != 0x06054B50) continue;
    const uint32_t cd_size = u32(&tail[i + 12]);
    const uint32_t cd_offset = u32(&tail[i + 16]);
    if (uint64_t{cd_offset} + cd_size > static_cast<uint64_t>(size)) return std::nullopt;
    std::vector<uint8_t> cd(cd_size);
    in.seekg(cd_offset);
    if (!in.read(reinterpret_cast<char*>(cd.data()), cd_size)) return std::nullopt;
    return Fnv(kFnvBasis, cd.data(), cd.size());
  }
  return std::nullopt;
}

std::string UnitCacheKey(const std::vector<ModUnitFile>& files, uint64_t base_identity) {
  uint64_t h = kFnvBasis;
  const uint32_t version = kUnitIndexVersion;
  h = Fnv(h, &version, sizeof(version));
  h = Fnv(h, &base_identity, sizeof(base_identity));
  for (const ModUnitFile& f : files) {
    h = Fnv(h, f.mod_folder.data(), f.mod_folder.size());
    h = Fnv(h, "\n", 1);
    h = Fnv(h, f.game_path.data(), f.game_path.size() * sizeof(char16_t));
    h = Fnv(h, &f.digest, sizeof(f.digest));
  }
  return Hex(h);
}

std::optional<fs::path> FindCachedUnitIndex(const fs::path& folder, const std::string& key, std::string* log) {
  const fs::path file = folder / (key + ".RAW");
  std::error_code ec;
  if (!fs::exists(file, ec)) return std::nullopt;
  const auto bytes = ReadAll(file);
  std::string error = "unreadable";
  if (bytes && ParseUnitIndex(*bytes, &error)) return file;
  if (log) *log = "cached unit index " + file.string() + " discarded: " + error;
  fs::remove(file, ec);
  return std::nullopt;
}

bool StoreCachedUnitIndex(const fs::path& folder, const std::string& key, const UnitIndex& index,
                          std::string* error) {
  std::error_code ec;
  fs::create_directories(folder, ec);
  const fs::path file = folder / (key + ".RAW");
  const fs::path temp = folder / (key + ".RAW.tmp");
  const std::vector<uint8_t> bytes = WriteUnitIndex(index);
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    out.flush();
    if (!out) {
      if (error) *error = "cannot write " + temp.string();
      fs::remove(temp, ec);
      return false;
    }
  }
  fs::rename(temp, file, ec);
  if (ec) {
    if (error) *error = "cannot rename " + temp.string() + ": " + ec.message();
    fs::remove(temp, ec);
    return false;
  }
  for (const auto& entry : fs::directory_iterator(folder, ec)) {
    const fs::path p = entry.path();
    if (p != file && (p.extension() == ".RAW" || p.extension() == ".tmp")) fs::remove(p, ec);
  }
  return true;
}

}  // namespace torchlight::mods
