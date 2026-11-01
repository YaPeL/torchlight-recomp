// Tests for the live frame queue: the producer never blocks; dropped frames keep their state and
// resources, lose their draws.

#include <cstdio>
#include <cstdlib>
#include <deque>
#include <random>
#include <thread>

#include "live/frame_queue.h"

namespace {

using namespace torchlight::commands;
using torchlight::live::FrameQueueDepth;
using torchlight::live::FrameQueue;
using torchlight::live::LiveFrame;
using torchlight::live::SnapshotStore;

void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    std::exit(1);
  }
}

LiveFrame MakeFrame(uint64_t swap, SnapshotStore& store) {
  LiveFrame f;
  f.swap = swap;
  f.commands.push_back({0, SetDepthCheck{swap % 2 == 0}});
  f.commands.push_back({0, Clear{}});
  f.commands.push_back({0, Draw{}});
  TextureDesc t;
  t.id = {ResourceKind::kTexture, uint32_t(0x1000 + swap), 1};
  f.textures.push_back(t);
  Blob b;
  b.bytes.assign(16, uint8_t(swap));
  ResourceId vb{ResourceKind::kVertexBuffer, 0x2000, 1};
  f.contents.push_back({vb, store.Put(vb, uint32_t(swap), b)});
  return f;
}

LiveFrame RandomFrame(uint64_t swap, std::mt19937& r, SnapshotStore& store) {
  LiveFrame f;
  f.swap = swap;
  for (unsigned i = 0, n = r() % 6; i < n; ++i) {
    switch (r() % 3) {
      case 0: f.commands.push_back({0, SetDepthCheck{r() % 2 == 0}}); break;
      case 1: f.commands.push_back({0, Clear{}}); break;
      default: f.commands.push_back({0, Draw{}}); break;
    }
  }
  if (r() % 2) {
    TextureDesc t;
    t.id = {ResourceKind::kTexture, uint32_t(swap), 1};
    f.textures.push_back(t);
  }
  if (r() % 3 == 0) f.destroyed.push_back({ResourceKind::kTexture, uint32_t(swap + 7), 1});
  if (r() % 2) {
    Blob b;
    b.bytes.assign(4, uint8_t(swap));
    ResourceId vb{ResourceKind::kVertexBuffer, uint32_t(swap), 1};
    f.contents.push_back({vb, store.Put(vb, uint32_t(swap), b)});
  }
  f.dropped_before = r() % 3 == 0 ? r() % 3 : 0;
  f.producer.guest_ms = r() % 1000 / 10.0;
  f.producer.worst_dropped_guest_ms = r() % 2 ? r() % 1000 / 10.0 : 0;
  f.producer.snapshot_copies = r() % 5;
  f.producer.snapshot_bytes = r() % 1000;
  return f;
}

bool SameFrame(const LiveFrame& a, const LiveFrame& b) {
  if (a.swap != b.swap || a.dropped_before != b.dropped_before ||
      a.commands.size() != b.commands.size() || a.textures.size() != b.textures.size() ||
      a.contents.size() != b.contents.size() || !(a.destroyed == b.destroyed)) {
    return false;
  }
  for (size_t i = 0; i < a.commands.size(); ++i) {
    if (!(a.commands[i].payload == b.commands[i].payload)) return false;
  }
  for (size_t i = 0; i < a.textures.size(); ++i) {
    if (!(a.textures[i].id == b.textures[i].id)) return false;
  }
  for (size_t i = 0; i < a.contents.size(); ++i) {
    if (!(a.contents[i].id == b.contents[i].id) ||
        a.contents[i].content->hash != b.contents[i].content->hash) {
      return false;
    }
  }
  return a.producer.guest_ms == b.producer.guest_ms &&
         a.producer.worst_dropped_guest_ms == b.producer.worst_dropped_guest_ms &&
         a.producer.snapshot_copies == b.producer.snapshot_copies &&
         a.producer.snapshot_bytes == b.producer.snapshot_bytes;
}

}  // namespace

int main() {
  SnapshotStore store;
  FrameQueue q(2);
  for (uint64_t swap = 1; swap <= 5; ++swap) q.Push(MakeFrame(swap, store));
  Check(q.pushed() == 5 && q.dropped() == 3, "three frames dropped with capacity 2");
  auto a = q.Pop(std::chrono::milliseconds(0));
  Check(a && a->swap == 4, "oldest kept frame first");
  // Frame 4 absorbed 1, 2 and 3: their state and resources, not their draws or clears.
  size_t draws = 0, clears = 0, state = 0;
  for (const auto& c : a->commands) {
    draws += std::holds_alternative<Draw>(c.payload);
    clears += std::holds_alternative<Clear>(c.payload);
    state += std::holds_alternative<SetDepthCheck>(c.payload);
  }
  Check(draws == 1 && clears == 1 && state == 4, "dropped frames keep state, lose draws");
  Check(std::get<SetDepthCheck>(a->commands.front().payload).enabled == false,
        "dropped state comes first, in order");
  Check(a->textures.size() == 4 && a->textures.front().id.guest_address == 0x1001,
        "dropped resources carried over in order");
  Check(a->contents.size() == 4 && a->dropped_before == 3, "contents kept alive, drops counted");
  auto b = q.Pop(std::chrono::milliseconds(0));
  Check(b && b->swap == 5 && b->dropped_before == 0, "newest frame intact");
  Check(!q.Pop(std::chrono::milliseconds(1)), "empty queue times out");

  // Consumer thread wakes on push and on close.
  std::thread consumer([&] {
    auto f = q.Pop(std::chrono::milliseconds(2000));
    Check(f && f->swap == 9, "consumer receives the frame");
    auto g = q.Pop(std::chrono::milliseconds(2000));
    Check(!g, "close wakes the consumer");
  });
  q.Push(MakeFrame(9, store));
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  q.Close();
  consumer.join();
  // The depth: one frame with vsync, two without, unless chosen; a deeper queue made shallower
  // drops the older frames at the next push.
  static_assert(FrameQueueDepth(0, true) == 1 && FrameQueueDepth(0, false) == 2);
  static_assert(FrameQueueDepth(3, true) == 3 && FrameQueueDepth(1, false) == 1);
  FrameQueue shallow;
  shallow.Push(MakeFrame(20, store));
  shallow.SetCapacity(1);
  shallow.Push(MakeFrame(21, store));
  auto newest = shallow.Pop(std::chrono::milliseconds(0));
  Check(newest && newest->swap == 21 && newest->dropped_before == 1 && shallow.dropped() == 1,
        "with one frame the newest is kept, the older dropped into it");
  shallow.SetCapacity(0);
  shallow.Push(MakeFrame(22, store));
  shallow.Push(MakeFrame(23, store));
  Check(shallow.dropped() == 2, "a capacity of 0 means one");

  // More drops than a frame holds unmerged (kMaxAbsorbedPending): the merged frame is the same as
  // merging each drop at once, and what the drops discarded stays with it until it is freed.
  FrameQueue deep(1);
  const uint64_t drops = 3 * torchlight::live::kMaxAbsorbedPending + 1;
  for (uint64_t swap = 100; swap <= 100 + drops; ++swap) deep.Push(MakeFrame(swap, store));
  auto merged = deep.Pop(std::chrono::milliseconds(0));
  Check(merged && merged->swap == 100 + drops && merged->dropped_before == drops &&
            merged->absorbed.empty(),
        "every drop merged at pop, counted");
  std::vector<bool> order;
  size_t merged_draws = 0;
  for (const auto& c : merged->commands) {
    merged_draws += std::holds_alternative<Draw>(c.payload);
    if (auto* d = std::get_if<SetDepthCheck>(&c.payload)) order.push_back(d->enabled);
  }
  bool alternating = order.size() == drops + 1;
  for (size_t i = 0; alternating && i < order.size(); ++i) alternating = order[i] == (i % 2 == 0);
  Check(alternating && merged_draws == 1, "the drops' state in order, their draws discarded");
  Check(merged->textures.size() == drops + 1 && merged->textures.front().id.guest_address == 0x1064 &&
            merged->contents.size() == drops + 1,
        "the drops' resources in order");
  // Past kMaxAbsorbedPending drops the oldest are merged and freed at push: the frame holds the
  // last ones' leftovers only, flat.
  size_t discarded_draws = 0;
  bool flat = true;
  for (const auto& d : merged->discarded) {
    flat = flat && d.discarded.empty() && d.absorbed.empty();
    for (const auto& c : d.commands) discarded_draws += std::holds_alternative<Draw>(c.payload);
  }
  Check(flat && merged->discarded.size() == torchlight::live::kMaxAbsorbedPending &&
            discarded_draws == torchlight::live::kMaxAbsorbedPending,
        "a stall holds the last drops' leftovers only, flat");

  // Against the old algorithm (each drop merged at once into the next frame): random pushes,
  // pops and capacity changes give the same frames, every field compared.
  for (unsigned seed = 0; seed < 200; ++seed) {
    std::mt19937 make_a(seed), make_b(seed), ops(seed * 7 + 1);
    FrameQueue queue(1 + seed % 3);
    std::deque<LiveFrame> reference;
    size_t capacity = 1 + seed % 3;
    uint64_t swap = 1;
    for (int step = 0; step < 300; ++step) {
      const unsigned op = ops() % 10;
      if (op < 7 + (seed % 2) * 2) {
        queue.Push(RandomFrame(swap, make_a, store));
        reference.push_back(RandomFrame(swap, make_b, store));
        ++swap;
        while (reference.size() > capacity) {
          LiveFrame oldest = std::move(reference.front());
          reference.pop_front();
          reference.front().AbsorbDropped(std::move(oldest));
        }
      } else if (op == 9 && seed % 5 == 0) {
        capacity = 1 + ops() % 3;
        queue.SetCapacity(capacity);
      } else {
        // Live mode pops unmerged and merges itself; both ways give the same frame.
        const bool merge = (step + seed) % 2 == 0;
        auto f = queue.Pop(std::chrono::milliseconds(0), merge);
        Check(bool(f) == !reference.empty(), "the same frames waiting as the old algorithm");
        if (f) {
          if (!merge) f->MergeAbsorbed();
          Check(f->absorbed.empty() &&
                    f->discarded.size() <= torchlight::live::kMaxAbsorbedPending,
                "merged frames hold at most kMaxAbsorbedPending drops' leftovers");
          Check(SameFrame(*f, reference.front()), "the same frame as the old algorithm");
          reference.pop_front();
        }
      }
    }
  }
  std::printf("frame queue test: ok\n");
  return 0;
}
