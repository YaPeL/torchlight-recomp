// Tests for resource identity: generations on address reuse, for every kind of reuse.

#include <cstdio>
#include <cstdlib>
#include <thread>

#include "capture/resource_registry.h"

namespace {

using namespace torchlight::commands;
using torchlight::capture::ResourceRegistry;

void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    std::exit(1);
  }
}

}  // namespace

int main() {
  ResourceRegistry r;

  // Constructed, destroyed, constructed again at the same address: a new generation.
  Check(!r.Create(ResourceKind::kVertexBuffer, 0x1000).has_value(), "first construction");
  auto a = r.Lookup(ResourceKind::kVertexBuffer, 0x1000);
  Check(a && a->id.generation == 1, "generation 1");
  auto destroyed = r.Destroy(0x1000);
  Check(destroyed && destroyed->generation == 1 && !r.Lookup(ResourceKind::kVertexBuffer, 0x1000),
        "destroyed");
  Check(!r.Create(ResourceKind::kVertexBuffer, 0x1000).has_value(), "reuse after destruction");
  Check(r.Lookup(ResourceKind::kVertexBuffer, 0x1000)->id.generation == 2, "generation 2");
  // A construction over an object whose destruction was not seen retires it.
  auto retired = r.Create(ResourceKind::kVertexBuffer, 0x1000);
  Check(retired && retired->generation == 2, "missed destruction retired");

  // Programs: same name at the same address keeps the identity; another name at a reused
  // address is a new generation and retires the old one.
  auto p1 = r.Program(0x2000, "123_VS");
  Check(!p1.retired && p1.id.generation == 1 && p1.id.kind == ResourceKind::kProgram, "program");
  auto p1b = r.Program(0x2000, "123_VS");
  Check(!p1b.retired && p1b.id.generation == 1, "same program created again: same identity");
  auto p2 = r.Program(0x2000, "456_FS");
  Check(p2.retired && p2.retired->generation == 1 && p2.id.generation == 2,
        "other program at a reused address: new generation");
  Check(r.Lookup(ResourceKind::kProgram, 0x2000)->id.generation == 2, "lookup sees it");

  // Textures: the first load keeps the constructor's generation, a reload starts a new one.
  r.Create(ResourceKind::kTexture, 0x3000);
  auto l1 = r.TextureLoaded(0x3000);
  Check(l1 && !l1->retired && l1->id.generation == 1, "first load");
  auto l2 = r.TextureLoaded(0x3000);
  Check(l2 && l2->retired && l2->retired->generation == 1 && l2->id.generation == 2,
        "reload: new generation, old one retired");
  Check(r.Lookup(ResourceKind::kTexture, 0x3000)->id.generation == 2, "lookup sees the reload");
  r.Destroy(0x3000);
  r.Create(ResourceKind::kTexture, 0x3000);
  auto l3 = r.TextureLoaded(0x3000);
  Check(l3 && !l3->retired && l3->id.generation == 3, "new texture at the address: first load");
  Check(!r.TextureLoaded(0x4000).has_value(), "unregistered texture");

  // Kinds do not alias: a program lookup at a texture's address finds nothing.
  Check(!r.Lookup(ResourceKind::kProgram, 0x3000), "kind checked");
  // ...also once both answers are cached.
  Check(r.Lookup(ResourceKind::kTexture, 0x3000).has_value(), "kind checked, cached (found)");
  Check(!r.Lookup(ResourceKind::kProgram, 0x3000), "kind checked, cached (not found)");

  // The lookup cache: repeated lookups give the same answer; a change on another thread is seen
  // by the next lookup; two registries never answer for each other.
  ResourceRegistry c;
  c.Create(ResourceKind::kVertexBuffer, 0x5000, {{}, 32, 10, 0});
  for (int i = 0; i < 3; ++i)
    Check(c.Lookup(ResourceKind::kVertexBuffer, 0x5000)->element_size == 32, "cached answer");
  std::thread([&] { c.Destroy(0x5000); }).join();
  Check(!c.Lookup(ResourceKind::kVertexBuffer, 0x5000), "a destruction on another thread is seen");
  std::thread([&] { c.Create(ResourceKind::kVertexBuffer, 0x5000, {{}, 48, 10, 0}); }).join();
  auto again = c.Lookup(ResourceKind::kVertexBuffer, 0x5000);
  Check(again && again->element_size == 48 && again->id.generation == 2,
        "a creation on another thread is seen");
  ResourceRegistry other;
  Check(!other.Lookup(ResourceKind::kVertexBuffer, 0x5000), "another registry: not cached across");
  other.Create(ResourceKind::kVertexBuffer, 0x5000, {{}, 16, 1, 0});
  Check(other.Lookup(ResourceKind::kVertexBuffer, 0x5000)->element_size == 16 &&
            c.Lookup(ResourceKind::kVertexBuffer, 0x5000)->element_size == 48,
        "each registry its own answer");
  // The cache is invalidated per bucket of addresses (Bucket: (address >> 2) & 255), so these
  // addresses share one cache slot and one version: a change at one of them must never leave a
  // stale answer valid for the other, and a reused address never answers with its old resource.
  {
    ResourceRegistry b;
    const uint32_t a1 = 0x10000, a2 = 0x10000 + 256 * 4;  // same bucket
    const uint32_t far = 0x10004;                          // another bucket
    b.Create(ResourceKind::kVertexBuffer, a1, {{}, 8, 1, 0});
    b.Create(ResourceKind::kVertexBuffer, a2, {{}, 12, 1, 0});
    b.Create(ResourceKind::kVertexBuffer, far, {{}, 20, 1, 0});
    Check(b.Lookup(ResourceKind::kVertexBuffer, a1)->element_size == 8, "bucket: first address");
    // A change at the other address of the bucket: the first one still answers right.
    b.Destroy(a2);
    Check(b.Lookup(ResourceKind::kVertexBuffer, a1)->element_size == 8,
          "bucket: a change at another address keeps the right answer");
    Check(!b.Lookup(ResourceKind::kVertexBuffer, a2), "bucket: the destroyed address is gone");
    // The slot now holds a2 (not found); a1 created again under it must be seen.
    b.Destroy(a1);
    b.Create(ResourceKind::kVertexBuffer, a1, {{}, 16, 1, 0});
    auto reused = b.Lookup(ResourceKind::kVertexBuffer, a1);
    Check(reused && reused->element_size == 16 && reused->id.generation == 2,
          "bucket: a reused address answers with its new resource");
    // Cache a1, then recreate a2 in the same bucket: a2 is seen, a1 keeps its answer.
    Check(b.Lookup(ResourceKind::kVertexBuffer, a1)->id.generation == 2, "bucket: cached again");
    b.Create(ResourceKind::kVertexBuffer, a2, {{}, 24, 1, 0});
    auto a2_again = b.Lookup(ResourceKind::kVertexBuffer, a2);
    Check(a2_again && a2_again->element_size == 24 && a2_again->id.generation == 2,
          "bucket: a creation at an address cached as missing is seen");
    Check(b.Lookup(ResourceKind::kVertexBuffer, a1)->id.generation == 2,
          "bucket: the other address is right after the slot moved");
    // Destroyed and recreated at the same address with no lookup in between: no stale answer.
    Check(b.Lookup(ResourceKind::kVertexBuffer, far)->element_size == 20, "other bucket: cached");
    b.Destroy(far);
    b.Create(ResourceKind::kVertexBuffer, far, {{}, 28, 1, 0});
    auto far_again = b.Lookup(ResourceKind::kVertexBuffer, far);
    Check(far_again && far_again->element_size == 28 && far_again->id.generation == 2,
          "same address destroyed and recreated between lookups: the new resource");
    // A texture reload (new generation, same address) is seen through the cache.
    b.Create(ResourceKind::kTexture, 0x20000);
    b.TextureLoaded(0x20000);
    Check(b.Lookup(ResourceKind::kTexture, 0x20000)->id.generation == 1, "texture: cached");
    b.TextureLoaded(0x20000);
    Check(b.Lookup(ResourceKind::kTexture, 0x20000)->id.generation == 2,
          "texture reload seen through the cache");
    // A program renewed at its address is seen through the cache.
    b.Program(0x30000, "1_VS");
    Check(b.Lookup(ResourceKind::kProgram, 0x30000)->id.generation == 1, "program: cached");
    b.Program(0x30000, "2_VS");
    Check(b.Lookup(ResourceKind::kProgram, 0x30000)->id.generation == 2,
          "program renewal seen through the cache");
  }
  std::printf("resource registry test: ok\n");
  return 0;
}
