// Decoded item texture images for the equipment texture build (docs/wardrobe-texture.md): the
// image the guest's Ogre::Image::load (guest_abi wardrobe kImageLoad) made from a texture file the
// first time, kept on the host and given back for the same file, so the build does not open and
// decode the file again at every equip. Files do not change while the game runs (the pak, and a
// mod's files, which are fixed at startup). Bounded in bytes: the least recently used images go
// first. No guest or SDK dependency.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <list>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace torchlight::hooks {

// An Ogre::Image after load, without its vtable and buffer pointer: the fields at +4 to +35
// (width, height, depth, size, mipmaps, flags, format, pixel size and its padding) as the guest
// stores them, and the buffer's bytes.
struct CachedImage {
  std::array<uint8_t, 32> fields{};
  std::vector<uint8_t> pixels;
};

// The key for a texture file: its name and resource group, lower case, with '/' for '\\'.
std::string ImageCacheKey(std::string_view name, std::string_view group);

class ImageCache {
 public:
  explicit ImageCache(size_t capacity_bytes) : capacity_(capacity_bytes) {}

  // A copy of the image for `key` (it becomes the most recent), or false.
  bool Find(const std::string& key, CachedImage& out);
  // Keeps the image (or replaces the one for `key`), dropping the least recent ones until it fits.
  // One larger than the whole capacity is not kept.
  void Insert(const std::string& key, CachedImage image);

  size_t bytes() const;
  size_t entries() const;
  size_t capacity() const { return capacity_; }
  uint64_t hits() const;
  uint64_t misses() const;

 private:
  struct Slot {
    std::string key;
    CachedImage image;
  };
  void DropLocked(std::list<Slot>::iterator it);

  const size_t capacity_;
  mutable std::mutex mutex_;
  std::list<Slot> order_;  // most recent first
  std::unordered_map<std::string, std::list<Slot>::iterator> index_;
  size_t bytes_ = 0;
  uint64_t hits_ = 0, misses_ = 0;
};

}  // namespace torchlight::hooks
