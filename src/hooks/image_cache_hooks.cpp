// The decoded item texture images of the equipment texture build (hooks/image_cache.h,
// docs/wardrobe-texture.md): the guest's Image::load (guest_abi wardrobe kImageLoad), when the
// wardrobe build calls it for an item's texture, gives back the image this file made before
// instead of opening and decoding it again. Every other caller loads as before.
//
// The build never writes the item's image (it only reads it while blending into the base, then
// deletes it), and the image owns its buffer, so every load still gets a buffer of its own: a new
// guest block with the cached bytes, which the image frees as it frees the codec's.
// --wardrobe_image_cache_mb: the bound (-1: the platform's default, 0: off).

#include "hooks/image_cache_hooks.h"

#include <chrono>
#include <cstring>
#include <memory>
#include <string>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <rex/system/xmemory.h>

#include "guest_abi/wardrobe.h"
#include "hooks/image_cache.h"
#include "platform/platform.h"

REXCVAR_DEFINE_INT32(wardrobe_image_cache_mb, -1, "Torchlight",
                     "Decoded item texture images kept for the equipment texture build, in MB "
                     "(-1: the platform's default, 0: off)");

namespace {

namespace wardrobe = torchlight::guest_abi::wardrobe;
namespace image = torchlight::guest_abi::wardrobe::image;

static_assert(wardrobe::kImageLoad.address == 0x82471380);
static_assert(wardrobe::kGuestAlloc.address == 0x821CD7F8);
static_assert(wardrobe::kGuestFree.address == 0x821CD9A8);

std::unique_ptr<torchlight::hooks::ImageCache> g_cache;

uint8_t* Guest(uint8_t* base, uint32_t address) {
  return static_cast<uint8_t*>(rex::memory::GuestPtr(base, address));
}
uint32_t Read32(uint8_t* base, uint32_t address) {
  const uint8_t* p = Guest(base, address);
  return uint32_t{p[0]} << 24 | uint32_t{p[1]} << 16 | uint32_t{p[2]} << 8 | p[3];
}
void Write32(uint8_t* base, uint32_t address, uint32_t value) {
  uint8_t* p = Guest(base, address);
  p[0] = static_cast<uint8_t>(value >> 24);
  p[1] = static_cast<uint8_t>(value >> 16);
  p[2] = static_cast<uint8_t>(value >> 8);
  p[3] = static_cast<uint8_t>(value);
}

// A guest std::string (MSVC: the characters inline at +0 while the capacity at +20 is below 16,
// else a pointer at +0; the length at +16), as Image::load reads its file name.
std::string ReadString(uint8_t* base, uint32_t str) {
  const uint32_t length = Read32(base, str + 16);
  const uint32_t capacity = Read32(base, str + 20);
  if (length > 1024) return {};
  const uint32_t text = capacity >= 16 ? Read32(base, str) : str;
  std::string out(length, '\0');
  for (uint32_t i = 0; i < length; ++i) out[i] = static_cast<char>(*Guest(base, text + i));
  return out;
}

}  // namespace

extern "C" {
REX_EXTERN(sub_821CD7F8);
REX_EXTERN(sub_821CD9A8);
}

namespace torchlight::hooks {

void InstallWardrobeImageCache() {
  const int32_t mb = REXCVAR_GET(wardrobe_image_cache_mb);
  const uint32_t bound = mb < 0 ? platform::DefaultHostCacheMB() : static_cast<uint32_t>(mb);
  if (bound) g_cache = std::make_unique<ImageCache>(size_t{bound} << 20);
  REXLOG_INFO("wardrobe item images: {} (--wardrobe_image_cache_mb={})",
              bound ? "kept, up to " + std::to_string(bound) + " MB" : std::string("decoded at every build"), mb);
}

}  // namespace torchlight::hooks

extern "C" {

REX_EXTERN(__imp__sub_82471380);
REX_FUNC(sub_82471380) {
  if (!g_cache || static_cast<uint32_t>(ctx.lr) != wardrobe::kItemImageLoadReturn) {
    __imp__sub_82471380(ctx, base);
    return;
  }
  const auto start = std::chrono::steady_clock::now();
  auto elapsed = [&start] {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  };
  const uint32_t img = ctx.r3.u32;
  const std::string key =
      torchlight::hooks::ImageCacheKey(ReadString(base, ctx.r4.u32), ReadString(base, ctx.r5.u32));
  torchlight::hooks::CachedImage cached;
  if (g_cache->Find(key, cached)) {
    // As the guest's load: the image's old buffer freed when it owns one, then the result in.
    if (const uint32_t old = Read32(base, img + image::kBuffer.offset); old && *Guest(base, img + image::kAutoDelete.offset)) {
      PPCContext free_ctx = ctx;
      free_ctx.r3.u64 = old;
      sub_821CD9A8(free_ctx, base);
      Write32(base, img + image::kBuffer.offset, 0);
    }
    PPCContext alloc = ctx;
    alloc.r3.u64 = 0;
    alloc.r4.u64 = cached.pixels.size();
    sub_821CD7F8(alloc, base);
    const uint32_t buffer = alloc.r3.u32;
    if (buffer) {
      std::memcpy(Guest(base, buffer), cached.pixels.data(), cached.pixels.size());
      std::memcpy(Guest(base, img + image::kFirstField.offset), cached.fields.data(), image::kFieldBytes);
      Write32(base, img + image::kBuffer.offset, buffer);
      *Guest(base, img + image::kAutoDelete.offset) = 1;
      ctx.r3.u64 = img;
      REXLOG_INFO("wardrobe item images: \"{}\" from the cache in {:.3f} ms", key.substr(0, key.find('\n')), elapsed());
      return;
    }
    // No memory: the guest's own load (which allocates the same way).
  }
  __imp__sub_82471380(ctx, base);
  const double load_ms = elapsed();
  const uint32_t size = Read32(base, img + image::kSize.offset);
  const uint32_t buffer = Read32(base, img + image::kBuffer.offset);
  if (!buffer || !size || size > g_cache->capacity()) return;
  torchlight::hooks::CachedImage made;
  std::memcpy(made.fields.data(), Guest(base, img + image::kFirstField.offset), image::kFieldBytes);
  made.pixels.resize(size);
  std::memcpy(made.pixels.data(), Guest(base, buffer), size);
  g_cache->Insert(key, std::move(made));
  REXLOG_INFO("wardrobe item images: kept \"{}\" ({} KB, decoded in {:.2f} ms); {} images, {} KB of {} MB",
              key.substr(0, key.find('\n')), size / 1024, load_ms, g_cache->entries(), g_cache->bytes() / 1024,
              g_cache->capacity() >> 20);
}

}  // extern "C"
