#include "capture/resource_registry.h"

#include <array>

namespace torchlight::capture {

using commands::ResourceId;
using commands::ResourceKind;

ResourceId ResourceRegistry::NewGeneration(ResourceKind kind, uint32_t address) {
  return {kind, address, ++generations_[address]};
}

std::optional<ResourceId> ResourceRegistry::Create(ResourceKind kind, uint32_t address,
                                                   BufferInfo info) {
  std::lock_guard<live::MeasuredMutex> lock(mutex_);
  std::optional<ResourceId> retired;
  auto it = live_.find(address);
  if (it != live_.end()) retired = it->second.info.id;
  info.id = NewGeneration(kind, address);
  live_[address] = {kind, info, {}, false};
  Touched(address);
  return retired;
}

std::optional<ResourceId> ResourceRegistry::Destroy(uint32_t address) {
  std::lock_guard<live::MeasuredMutex> lock(mutex_);
  auto it = live_.find(address);
  if (it == live_.end()) return std::nullopt;
  ResourceId id = it->second.info.id;
  live_.erase(it);
  Touched(address);
  return id;
}

std::optional<BufferInfo> ResourceRegistry::Lookup(ResourceKind kind, uint32_t address) const {
  struct Entry {
    uint64_t instance = 0, version = 0;
    uint32_t address = 0;
    ResourceKind kind = ResourceKind::kVertexBuffer;
    std::optional<BufferInfo> answer;
  };
  thread_local std::array<Entry, kCacheSlots> cache;
  const size_t bucket = Bucket(address);
  Entry& e = cache[bucket];
  if (e.instance == instance_ && e.address == address && e.kind == kind &&
      e.version == bucket_versions_[bucket].load(std::memory_order_acquire)) {
    return e.answer;
  }
  std::lock_guard<live::MeasuredMutex> lock(mutex_);
  auto it = live_.find(address);
  std::optional<BufferInfo> answer;
  if (it != live_.end() && it->second.kind == kind) answer = it->second.info;
  e = {instance_, bucket_versions_[bucket].load(std::memory_order_relaxed), address, kind, answer};
  return answer;
}

ResourceRegistry::Renewal ResourceRegistry::Program(uint32_t address, const std::string& name) {
  std::lock_guard<live::MeasuredMutex> lock(mutex_);
  auto it = live_.find(address);
  if (it != live_.end() && it->second.kind == ResourceKind::kProgram && it->second.name == name) {
    return {it->second.info.id, std::nullopt};
  }
  Renewal r;
  if (it != live_.end()) r.retired = it->second.info.id;
  BufferInfo info;
  info.id = NewGeneration(ResourceKind::kProgram, address);
  live_[address] = {ResourceKind::kProgram, info, name, false};
  Touched(address);
  r.id = info.id;
  return r;
}

std::optional<ResourceRegistry::Renewal> ResourceRegistry::TextureLoaded(uint32_t address) {
  std::lock_guard<live::MeasuredMutex> lock(mutex_);
  auto it = live_.find(address);
  if (it == live_.end() || it->second.kind != ResourceKind::kTexture) return std::nullopt;
  Live& live = it->second;
  if (!live.loaded) {
    live.loaded = true;
    return Renewal{live.info.id, std::nullopt};
  }
  Renewal r;
  r.retired = live.info.id;
  live.info.id = NewGeneration(ResourceKind::kTexture, address);
  Touched(address);
  r.id = live.info.id;
  return r;
}

}  // namespace torchlight::capture
