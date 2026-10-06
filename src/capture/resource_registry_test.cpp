// Tests for resource identity: generations on address reuse, for every kind of reuse.

#include <cstdio>
#include <cstdlib>

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
  std::printf("resource registry test: ok\n");
  return 0;
}
