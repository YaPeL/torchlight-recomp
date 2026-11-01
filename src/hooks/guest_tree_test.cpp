// Tests for the host tree walks (guest_tree.h) over a fake guest address space: MSVC trees built in
// guest memory, the map find against a lower bound and the iterator increments against an in-order
// walk, for both isnil offsets.

#include <cstdio>
#include <cstdlib>
#include <map>
#include <vector>

#include "hooks/guest_tree.h"

namespace {

namespace abi = torchlight::guest_abi;
using namespace torchlight::hooks::guest_tree;

void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    std::exit(1);
  }
}

struct Guest {
  std::vector<uint8_t> memory = std::vector<uint8_t>(1u << 20);
  uint8_t* base() { return memory.data(); }
  uint32_t next = 0x1000;
  uint32_t Alloc(uint32_t size) {
    const uint32_t at = next;
    next += (size + 15) & ~15u;
    return at;
  }
  void W32(uint32_t address, uint32_t value) { abi::WriteU32(base(), address, value); }
  void W8(uint32_t address, uint8_t value) { abi::WriteBytes(base(), address, &value, 1); }
};

// An MSVC tree of `keys` (sorted) with the isnil byte at `isnil`: the head is the nil node (its
// parent the root, left the leftmost, right the rightmost); a node's missing children are the head.
struct Tree {
  uint32_t head = 0;
  std::vector<uint32_t> nodes;  // in key order
};

uint32_t Build(Guest& g, const Tree& t, const std::vector<uint32_t>& keys, size_t lo, size_t hi,
               uint32_t parent) {
  if (lo >= hi) return t.head;
  const size_t mid = (lo + hi) / 2;
  const uint32_t n = t.nodes[mid];
  g.W32(n + 4, parent);
  g.W32(n + 12, keys[mid]);
  g.W32(n + 0, Build(g, t, keys, lo, mid, n));
  g.W32(n + 8, Build(g, t, keys, mid + 1, hi, n));
  return n;
}

Tree MakeTree(Guest& g, const std::vector<uint32_t>& keys, uint32_t isnil) {
  Tree t;
  t.head = g.Alloc(32);
  g.W8(t.head + isnil, 1);
  for (size_t i = 0; i < keys.size(); ++i) t.nodes.push_back(g.Alloc(32));  // isnil 0
  const uint32_t root = Build(g, t, keys, 0, keys.size(), t.head);
  g.W32(t.head + 4, root);
  g.W32(t.head + 0, keys.empty() ? t.head : t.nodes.front());
  g.W32(t.head + 8, keys.empty() ? t.head : t.nodes.back());
  return t;
}

void TestFind(const std::vector<uint32_t>& keys) {
  Guest g;
  const Tree t = MakeTree(g, keys, 21);
  const uint32_t map = g.Alloc(16), key = g.Alloc(4), result = g.Alloc(4);
  g.W32(map + 4, t.head);
  const uint32_t stack = 0x80000;
  for (uint32_t probe = 0; probe <= (keys.empty() ? 4 : keys.back() + 2); ++probe) {
    g.W32(key, probe);
    PPCContext ctx{};
    ctx.r1.u64 = stack;
    ctx.r3.u64 = result;
    ctx.r4.u64 = map;
    ctx.r5.u64 = key;
    const Registers r = DeviceMapFind(ctx, g.base(), MemoryStores{g.base()});
    // std::map::find: the node with the key, else the end (the head).
    uint32_t want = t.head;
    for (size_t i = 0; i < keys.size(); ++i) {
      if (keys[i] == probe) want = t.nodes[i];
    }
    Check(abi::ReadU32(g.base(), result) == want, "find stores the key's node or the head");
    Check(r.r11 && *r.r11 == want && abi::ReadU32(g.base(), stack - 16) == (r.r10 ? *r.r10 : 0),
          "find leaves the result in r11 and its candidate in r10 and at r1-16");
  }
}

template <uint32_t kIsNil>
void TestNext(const std::vector<uint32_t>& keys) {
  Guest g;
  const Tree t = MakeTree(g, keys, kIsNil);
  const uint32_t iterator = g.Alloc(4);
  PPCContext ctx{};
  ctx.r1.u64 = 0x80000;
  ctx.r3.u64 = iterator;
  g.W32(iterator, keys.empty() ? t.head : t.nodes.front());
  for (size_t i = 1; i <= keys.size(); ++i) {
    TreeNext<kIsNil>(ctx, g.base(), MemoryStores{g.base()});
    const uint32_t want = i < keys.size() ? t.nodes[i] : t.head;
    Check(abi::ReadU32(g.base(), iterator) == want,
          "++ goes to the next node in order, then the head");
  }
  TreeNext<kIsNil>(ctx, g.base(), MemoryStores{g.base()});
  Check(abi::ReadU32(g.base(), iterator) == t.head, "++ on the head stays");
  // Recorded stores give the same final value as stores made in memory.
  if (!keys.empty()) {
    for (size_t i = 0; i + 1 < keys.size(); ++i) {
      g.W32(iterator, t.nodes[i]);
      RecordedStores recorded;
      TreeNext<kIsNil>(ctx, g.base(), recorded);
      Check(!recorded.overflow && recorded.count == 1 && recorded.stores[0].address == iterator &&
                recorded.stores[0].value == t.nodes[i + 1],
            "recorded stores keep the last value per address");
    }
  }
}

}  // namespace

int main() {
  for (std::vector<uint32_t> keys : {std::vector<uint32_t>{}, {5}, {1, 2}, {3, 9, 27},
                                     {2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26}}) {
    TestFind(keys);
    TestNext<17>(keys);
    TestNext<21>(keys);
  }
  std::vector<uint32_t> many;
  for (uint32_t k = 1; k < 300; k += 3) many.push_back(k);
  TestFind(many);
  TestNext<17>(many);
  TestNext<21>(many);
  std::printf("guest tree test: ok\n");
  return 0;
}
