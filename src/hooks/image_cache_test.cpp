// hooks/image_cache.h: keys, copies, LRU order and the byte bound.

#include "hooks/image_cache.h"

#include <cstdio>
#include <string>

namespace {

int failures = 0;
void Check(bool ok, const char* what) {
  if (!ok) {
    ++failures;
    std::fprintf(stderr, "FAIL: %s\n", what);
  }
}

torchlight::hooks::CachedImage Image(size_t bytes, uint8_t fill) {
  torchlight::hooks::CachedImage image;
  image.fields[0] = fill;
  image.pixels.assign(bytes, fill);
  return image;
}

}  // namespace

int main() {
  using torchlight::hooks::ImageCache;
  using torchlight::hooks::ImageCacheKey;

  Check(ImageCacheKey("Media\\Wardrobe\\Destroyer\\COWL.DDS", "General") ==
            ImageCacheKey("media/wardrobe/destroyer/cowl.dds", "general"),
        "case and separators do not matter");
  Check(ImageCacheKey("a.dds", "General") != ImageCacheKey("a.dds", "Mods"), "the group is part of the key");
  Check(ImageCacheKey("ab", "c") != ImageCacheKey("a", "bc"), "name and group do not run together");

  ImageCache cache(100);
  torchlight::hooks::CachedImage got;
  Check(!cache.Find("x", got), "empty");
  cache.Insert("a", Image(40, 1));
  cache.Insert("b", Image(40, 2));
  Check(cache.Find("a", got) && got.pixels.size() == 40 && got.pixels[0] == 1 && got.fields[0] == 1, "a found");
  got.pixels[0] = 99;  // a copy: the cached image does not change
  Check(cache.Find("a", got) && got.pixels[0] == 1, "a copy is given");
  cache.Insert("c", Image(40, 3));  // over 100: the least recent (b) goes
  Check(!cache.Find("b", got), "b dropped (least recent)");
  Check(cache.Find("a", got) && cache.Find("c", got), "a and c kept");
  Check(cache.bytes() == 80 && cache.entries() == 2, "bytes and entries");
  cache.Insert("a", Image(10, 4));  // replaced
  Check(cache.Find("a", got) && got.pixels.size() == 10 && got.pixels[0] == 4, "a replaced");
  Check(cache.bytes() == 50, "bytes after replacing");
  cache.Insert("big", Image(101, 5));
  Check(!cache.Find("big", got) && cache.bytes() == 50, "an image over the capacity is not kept");
  cache.Insert("d", Image(60, 6));  // 50 + 60 > 100: c (least recent) goes, a stays
  Check(cache.Find("d", got) && cache.Find("a", got) && !cache.Find("c", got), "drops until it fits");
  Check(cache.hits() > 0 && cache.misses() > 0, "counters");

  std::printf("image_cache_test: %d failures\n", failures);
  return failures ? 1 : 0;
}
