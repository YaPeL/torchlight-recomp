// Tests for the live snapshot store: one copy per version, superseded and destroyed content freed.

#include <cstdio>
#include <cstdlib>
#include <vector>

#include "live/snapshot_store.h"

namespace {

using namespace torchlight::commands;
using torchlight::live::SnapshotStore;

void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    std::exit(1);
  }
}

Blob Bytes(size_t n) {
  Blob b;
  b.bytes.assign(n, 0xAB);
  return b;
}

ResourceId Vb(uint32_t address, uint32_t generation) {
  return {ResourceKind::kVertexBuffer, address, generation};
}

}  // namespace

int main() {
  SnapshotStore store;

  // One snapshot per version; a new version replaces the old one in the store.
  auto v1 = store.Put(Vb(0x1000, 1), 1, Bytes(100));
  Check(store.Find(Vb(0x1000, 1), 1) == v1, "find the stored version");
  Check(v1->hash == SnapshotStore::Key(Vb(0x1000, 1), 1), "hash is the content key");
  auto v2 = store.Put(Vb(0x1000, 1), 2, Bytes(200));
  Check(!store.Find(Vb(0x1000, 1), 1) && store.Find(Vb(0x1000, 1), 2) == v2, "latest only");
  Check(store.bytes() == 200 && store.resources() == 1, "store holds the latest version");
  // A frame that still holds the old version keeps it alive until it is done.
  std::weak_ptr<const Blob> old = v1;
  Check(!old.expired(), "old version alive while a frame holds it");
  v1.reset();
  Check(old.expired(), "old version freed when the last frame drops it");
  v2.reset();
  Check(store.Find(Vb(0x1000, 1), 2) != nullptr, "store keeps the latest version");

  // Keys: distinct per version, generation and kind.
  Check(SnapshotStore::Key(Vb(0x1000, 1), 1) != SnapshotStore::Key(Vb(0x1000, 1), 2), "key by version");
  Check(SnapshotStore::Key(Vb(0x1000, 1), 1) != SnapshotStore::Key(Vb(0x1000, 2), 1), "key by generation");
  Check(SnapshotStore::Key(Vb(0x1000, 1), 1) !=
            SnapshotStore::Key({ResourceKind::kIndexBuffer, 0x1000, 1}, 1),
        "key by kind");

  // Buffers created, updated and destroyed in a loop: memory does not grow.
  store.Release(Vb(0x1000, 1));
  Check(store.bytes() == 0 && store.resources() == 0, "release empties the store");
  std::vector<std::weak_ptr<const Blob>> seen;
  for (uint32_t generation = 1; generation <= 2000; ++generation) {
    ResourceId id = Vb(0x2000 + (generation % 8) * 0x100, generation);
    for (uint32_t version = 1; version <= 5; ++version) {
      auto c = store.Put(id, version, Bytes(4096));
      if (generation % 100 == 0) seen.push_back(c);
    }
    store.Release(id);
    Check(store.bytes() == 0 && store.resources() == 0, "nothing retained after destroy");
  }
  for (const auto& w : seen) Check(w.expired(), "no snapshot outlives its resource and frames");

  // Many live resources at once: one snapshot each, whatever the number of versions.
  for (uint32_t i = 0; i < 64; ++i)
    for (uint32_t version = 1; version <= 50; ++version) store.Put(Vb(0x9000 + i, 1), version, Bytes(1000));
  Check(store.resources() == 64 && store.bytes() == 64 * 1000, "one snapshot per live resource");

  std::printf("snapshot store test: ok\n");
  return 0;
}
