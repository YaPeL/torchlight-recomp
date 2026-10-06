// Tests for the live content source: content superseded within a frame stays until the frame is
// done, destroyed resources are dropped, held content does not grow.

#include <cstdio>
#include <cstdlib>

#include "live/live_content_source.h"

namespace {

using namespace torchlight::commands;
using torchlight::live::LiveContentSource;
using torchlight::live::LiveFrame;
using torchlight::live::SnapshotStore;

void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    std::exit(1);
  }
}

Blob Bytes(size_t n) {
  Blob b;
  b.bytes.assign(n, 1);
  return b;
}

}  // namespace

int main() {
  torchlight::frontend::ZipArchive pak;  // not opened: no game files needed here
  LiveContentSource source(pak);
  SnapshotStore store;
  ResourceId vb{ResourceKind::kVertexBuffer, 0x1000, 1};

  // Two versions of one buffer in the same frame: both usable until the frame finishes.
  LiveFrame f;
  auto v1 = store.Put(vb, 1, Bytes(64));
  auto v2 = store.Put(vb, 2, Bytes(64));
  f.contents.push_back({vb, v1});
  f.contents.push_back({vb, v2});
  f.vertex_buffers.push_back({vb, 16, 4, 0});
  source.Apply(f);
  Check(source.Blob(v1->hash) && source.Blob(v2->hash), "both versions during the frame");
  auto freed = source.Finish();
  Check(freed.size() == 1 && freed[0] == v1->hash, "superseded version freed after the frame");
  Check(!source.Blob(v1->hash) && source.Blob(v2->hash), "latest version kept");
  Check(source.VertexBuffer(vb) != nullptr, "description kept");

  // Destroyed: content and description dropped; textures reported for release.
  LiveFrame g;
  ResourceId tex{ResourceKind::kTexture, 0x2000, 1};
  TextureDesc t;
  t.id = tex;
  t.name = "rtt/1/207";
  t.render_target = true;
  g.textures.push_back(t);
  source.Apply(g);
  source.Finish();
  LiveFrame h;
  h.destroyed.push_back(vb);
  h.destroyed.push_back(tex);
  auto changes = source.Apply(h);
  freed = source.Finish();
  Check(freed.size() == 1 && freed[0] == v2->hash, "destroyed buffer content freed");
  Check(!source.VertexBuffer(vb) && !source.Texture(tex), "descriptions dropped");
  Check(changes.destroyed_textures.size() == 1, "destroyed texture reported");
  Check(changes.destroyed_buffers.size() == 1 && changes.destroyed_buffers[0] == vb,
        "destroyed buffer reported (its host buffers are freed)");
  Check(source.content_bytes() == 0, "nothing held");

  // Buffers created, updated and destroyed in a loop: held content does not grow.
  for (uint32_t gen = 2; gen < 2002; ++gen) {
    ResourceId id{ResourceKind::kVertexBuffer, 0x3000, gen};
    for (uint32_t version = 1; version <= 3; ++version) {
      LiveFrame frame;
      frame.contents.push_back({id, store.Put(id, version, Bytes(4096))});
      source.Apply(frame);
      source.Finish();
    }
    LiveFrame end;
    end.destroyed.push_back(id);
    source.Apply(end);
    source.Finish();
    store.Release(id);
    Check(source.content_bytes() == 0, "no content left after destroy");
  }
  // Identical content in two buffers: superseded in one, still the other's latest, so kept.
  {
    ResourceId a{ResourceKind::kVertexBuffer, 0x6000, 1}, b{ResourceKind::kVertexBuffer, 0x7000, 1};
    auto shared = store.Put(a, 1, Bytes(32));
    LiveFrame both;
    both.contents.push_back({a, shared});
    both.contents.push_back({b, shared});
    source.Apply(both);
    source.Finish();
    LiveFrame next;
    next.contents.push_back({a, store.Put(a, 2, Bytes(48))});
    source.Apply(next);
    auto kept = source.Finish();
    Check(kept.empty() && source.Blob(shared->hash), "content shared with another buffer kept");
    LiveFrame gone;
    gone.destroyed.push_back(a);
    gone.destroyed.push_back(b);
    source.Apply(gone);
    source.Finish();
    Check(!source.Blob(shared->hash), "shared content freed once nobody holds it");
  }
  // Superseded and current again within one frame (v1, v2, v1): v1 stays, v2 is freed.
  {
    ResourceId c{ResourceKind::kVertexBuffer, 0x8000, 1};
    auto w1 = store.Put(c, 1, Bytes(16));
    auto w2 = store.Put(c, 2, Bytes(24));
    LiveFrame f3;
    f3.contents.push_back({c, w1});
    f3.contents.push_back({c, w2});
    f3.contents.push_back({c, w1});
    source.Apply(f3);
    auto out = source.Finish();
    Check(out.size() == 1 && out[0] == w2->hash && source.Blob(w1->hash),
          "back to the first version: it stays");
  }
  // A program retired and another created at the same address: names by identity.
  LiveFrame p1;
  p1.programs.push_back({{ResourceKind::kProgram, 0x5000, 1}, "111_VS", {}});
  source.Apply(p1);
  LiveFrame p2;
  p2.destroyed.push_back({ResourceKind::kProgram, 0x5000, 1});
  p2.programs.push_back({{ResourceKind::kProgram, 0x5000, 2}, "222_FS", {}});
  source.Apply(p2);
  Check(source.ProgramName({ResourceKind::kProgram, 0x5000, 2}) == "222_FS" &&
            source.ProgramName({ResourceKind::kProgram, 0x5000, 1}).empty(),
        "program at a reused address: the new one, the retired one gone");
  std::printf("live content source test: ok\n");
  return 0;
}
