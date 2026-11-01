// Tests for the live frame queue: the producer never blocks; dropped frames keep their state and
// resources, lose their draws.

#include <cstdio>
#include <cstdlib>
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
  size_t discarded_draws = 0;
  std::vector<const LiveFrame*> pending;
  for (const auto& d : merged->discarded) pending.push_back(&d);
  while (!pending.empty()) {
    const LiveFrame* f = pending.back();
    pending.pop_back();
    for (const auto& c : f->commands) discarded_draws += std::holds_alternative<Draw>(c.payload);
    for (const auto& d : f->discarded) pending.push_back(&d);
  }
  Check(discarded_draws == drops, "the drops' draws held until the frame is freed");
  std::printf("frame queue test: ok\n");
  return 0;
}
