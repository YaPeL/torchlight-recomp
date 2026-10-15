#include "mods/settled_saves.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "mods/class_list.h"
#include "save_import/digest.h"

namespace torchlight::mods {

namespace {

constexpr const char* kHeader = "torchlight settled saves 1";

std::string Hex(const save_import::Sha256Digest& digest) {
  std::string out;
  char byte[3];
  for (uint8_t b : digest) {
    std::snprintf(byte, sizeof(byte), "%02x", b);
    out += byte;
  }
  return out;
}

}  // namespace

std::string SettledSaves::Context(const KnownUnits& known, const std::vector<std::u16string>& repair_names,
                                  const std::vector<std::u16string>& active_mods) {
  std::vector<int64_t> guids(known.guids.begin(), known.guids.end());
  std::sort(guids.begin(), guids.end());
  std::vector<std::string> names;
  for (const auto& name : repair_names) names.push_back(Utf16ToUtf8(name));
  std::sort(names.begin(), names.end());
  names.erase(std::unique(names.begin(), names.end()), names.end());
  std::string text = "rules " + std::to_string(kRulesVersion) + "\ncomplete " + (known.complete ? "1" : "0") + "\n";
  for (int64_t guid : guids) text += std::to_string(guid) + "\n";
  text += "names\n";
  for (const auto& name : names) text += name + "\n";
  // In order, as the game compares a save's list with them.
  text += "active\n";
  for (const auto& name : active_mods) text += Utf16ToUtf8(name) + "\n";
  return Hex(save_import::Sha256(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size())));
}

bool SettledSaves::StampOf(const std::filesystem::path& path, Stamp& out) {
  std::error_code ec;
  const uintmax_t size = std::filesystem::file_size(path, ec);
  if (ec) return false;
  const auto time = std::filesystem::last_write_time(path, ec);
  if (ec) return false;
  out = {size, static_cast<int64_t>(time.time_since_epoch().count())};
  return true;
}

std::string SettledSaves::Key(const std::filesystem::path& save) const {
  const std::u8string text = save.lexically_relative(root_).generic_u8string();
  return std::string(text.begin(), text.end());
}

SettledSaves SettledSaves::Load(const std::filesystem::path& file, const std::filesystem::path& root,
                                const std::string& context) {
  SettledSaves settled(root, context);
  std::ifstream in(file, std::ios::binary);
  std::string line;
  if (!std::getline(in, line) || line != kHeader) return settled;
  if (!std::getline(in, line) || line != "context " + context) return settled;
  while (std::getline(in, line)) {
    // "<size> <time> <path relative to the root>"
    std::istringstream fields(line);
    Stamp stamp;
    if (!(fields >> stamp.size >> stamp.time)) return SettledSaves(root, context);
    fields.get();
    std::string key;
    std::getline(fields, key);
    if (key.empty()) return SettledSaves(root, context);
    settled.entries_[key] = stamp;
  }
  return settled;
}

bool SettledSaves::Unchanged(const std::filesystem::path& save) const {
  const auto it = entries_.find(Key(save));
  Stamp now;
  return it != entries_.end() && StampOf(save, now) && now == it->second;
}

void SettledSaves::Settle(const std::filesystem::path& save) {
  Stamp now;
  if (StampOf(save, now)) {
    entries_[Key(save)] = now;
  } else {
    entries_.erase(Key(save));
  }
}

bool SettledSaves::Store(const std::filesystem::path& file) const {
  std::error_code ec;
  std::filesystem::create_directories(file.parent_path(), ec);
  const std::filesystem::path temp = file.string() + ".tmp";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    out << kHeader << "\ncontext " << context_ << "\n";
    for (const auto& [key, stamp] : entries_) out << stamp.size << " " << stamp.time << " " << key << "\n";
    out.flush();
    if (!out) {
      std::filesystem::remove(temp, ec);
      return false;
    }
  }
  std::filesystem::rename(temp, file, ec);
  if (ec) std::filesystem::remove(temp, ec);
  return !ec;
}

}  // namespace torchlight::mods
