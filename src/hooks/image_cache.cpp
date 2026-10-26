#include "hooks/image_cache.h"

namespace torchlight::hooks {

std::string ImageCacheKey(std::string_view name, std::string_view group) {
  std::string key;
  key.reserve(name.size() + group.size() + 1);
  auto add = [&key](std::string_view text) {
    for (char c : text) {
      if (c == '\\') c = '/';
      if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
      key += c;
    }
  };
  add(name);
  key += '\n';
  add(group);
  return key;
}

bool ImageCache::Find(const std::string& key, CachedImage& out) {
  std::lock_guard lock(mutex_);
  auto it = index_.find(key);
  if (it == index_.end()) {
    ++misses_;
    return false;
  }
  order_.splice(order_.begin(), order_, it->second);
  out = it->second->image;
  ++hits_;
  return true;
}

void ImageCache::DropLocked(std::list<Slot>::iterator it) {
  bytes_ -= it->image.pixels.size();
  index_.erase(it->key);
  order_.erase(it);
}

void ImageCache::Insert(const std::string& key, CachedImage image) {
  std::lock_guard lock(mutex_);
  if (auto it = index_.find(key); it != index_.end()) DropLocked(it->second);
  const size_t size = image.pixels.size();
  if (size > capacity_) return;
  while (bytes_ + size > capacity_ && !order_.empty()) DropLocked(std::prev(order_.end()));
  order_.push_front(Slot{key, std::move(image)});
  index_[key] = order_.begin();
  bytes_ += size;
}

size_t ImageCache::bytes() const {
  std::lock_guard lock(mutex_);
  return bytes_;
}
size_t ImageCache::entries() const {
  std::lock_guard lock(mutex_);
  return order_.size();
}
uint64_t ImageCache::hits() const {
  std::lock_guard lock(mutex_);
  return hits_;
}
uint64_t ImageCache::misses() const {
  std::lock_guard lock(mutex_);
  return misses_;
}

}  // namespace torchlight::hooks
