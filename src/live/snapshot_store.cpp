#include "live/snapshot_store.h"

namespace torchlight::live {

using commands::Hash;
using commands::ResourceId;

uint64_t SnapshotStore::Identity(const ResourceId& id) {
  return uint64_t(id.guest_address) | uint64_t(id.generation) << 32;
}

Hash SnapshotStore::Key(const ResourceId& id, uint32_t version) {
  // splitmix64 of (kind, address, generation, version): distinct from capture content hashes
  // only by convention, the two never meet in one content source.
  uint64_t x = Identity(id) ^ (uint64_t(version) * 0x9E3779B97F4A7C15ull) ^
               (uint64_t(uint8_t(id.kind)) << 56);
  x += 0x9E3779B97F4A7C15ull;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
  x ^= x >> 31;
  return x == 0 ? 1 : x;  // 0 means "no content"
}

SnapshotStore::Content SnapshotStore::Find(const ResourceId& id, uint32_t version) const {
  std::lock_guard<MeasuredMutex> lock(mutex_);
  auto it = entries_.find(Identity(id));
  if (it == entries_.end() || it->second.version != version) return nullptr;
  return it->second.content;
}

SnapshotStore::Content SnapshotStore::Put(const ResourceId& id, uint32_t version,
                                          commands::Blob blob) {
  blob.hash = Key(id, version);
  auto content = std::make_shared<const commands::Blob>(std::move(blob));
  std::lock_guard<MeasuredMutex> lock(mutex_);
  Entry& e = entries_[Identity(id)];
  if (e.content) bytes_ -= e.content->bytes.size();
  e.version = version;
  e.content = content;
  bytes_ += content->bytes.size();
  return content;
}

void SnapshotStore::Release(const ResourceId& id) {
  std::lock_guard<MeasuredMutex> lock(mutex_);
  auto it = entries_.find(Identity(id));
  if (it == entries_.end()) return;
  if (it->second.content) bytes_ -= it->second.content->bytes.size();
  entries_.erase(it);
}

size_t SnapshotStore::resources() const {
  std::lock_guard<MeasuredMutex> lock(mutex_);
  return entries_.size();
}

size_t SnapshotStore::bytes() const {
  std::lock_guard<MeasuredMutex> lock(mutex_);
  return bytes_;
}

}  // namespace torchlight::live
