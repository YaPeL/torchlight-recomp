#include "hooks/missing_file_cache.h"

#include <mutex>

namespace torchlight::hooks {

namespace {
// NTSTATUS values for a file or folder that is not there.
constexpr uint32_t kNoSuchFile = 0xC000000F;          // STATUS_NO_SUCH_FILE
constexpr uint32_t kObjectNameNotFound = 0xC0000034;  // STATUS_OBJECT_NAME_NOT_FOUND
constexpr uint32_t kObjectPathNotFound = 0xC000003A;  // STATUS_OBJECT_PATH_NOT_FOUND
constexpr uint32_t kNoMoreFiles = 0x80000006;         // STATUS_NO_MORE_FILES (an empty listing)
constexpr std::string_view kGameDevice = "GAME:";
}  // namespace

std::optional<std::string> MissingFileCache::Key(std::string_view guest_path) {
  std::string key;
  key.reserve(guest_path.size());
  for (char c : guest_path) {
    if (c == '*' || c == '?') return std::nullopt;
    if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    key += c;
  }
  if (key.size() <= kGameDevice.size() || key.compare(0, kGameDevice.size(), kGameDevice) != 0)
    return std::nullopt;
  return key;
}

bool MissingFileCache::IsNotFound(uint32_t status) {
  return status == kNoSuchFile || status == kObjectNameNotFound || status == kObjectPathNotFound ||
         status == kNoMoreFiles;
}

std::optional<uint32_t> MissingFileCache::Find(const std::string& key) const {
  std::shared_lock lock(mutex_);
  auto it = missing_.find(key);
  if (it == missing_.end()) return std::nullopt;
  return it->second;
}

void MissingFileCache::Remember(const std::string& key, uint32_t status) {
  if (!IsNotFound(status)) return;
  std::unique_lock lock(mutex_);
  missing_.emplace(key, status);
}

size_t MissingFileCache::size() const {
  std::shared_lock lock(mutex_);
  return missing_.size();
}

}  // namespace torchlight::hooks
