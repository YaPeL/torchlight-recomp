#include "capture/resource_registry.h"

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
  return retired;
}

std::optional<ResourceId> ResourceRegistry::Destroy(uint32_t address) {
  std::lock_guard<live::MeasuredMutex> lock(mutex_);
  auto it = live_.find(address);
  if (it == live_.end()) return std::nullopt;
  ResourceId id = it->second.info.id;
  live_.erase(it);
  return id;
}

std::optional<BufferInfo> ResourceRegistry::Lookup(ResourceKind kind, uint32_t address) const {
  std::lock_guard<live::MeasuredMutex> lock(mutex_);
  auto it = live_.find(address);
  if (it == live_.end() || it->second.kind != kind) return std::nullopt;
  return it->second.info;
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
  r.id = live.info.id;
  return r;
}

}  // namespace torchlight::capture
