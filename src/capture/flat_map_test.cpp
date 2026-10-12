// FlatMap against std::unordered_map: random inserts, lookups and erases, with the real hash and
// with one that sends many keys to the same slot (long probe chains across the wrap-around, where
// erase's back shift is easiest to get wrong).

#include <cstdio>
#include <cstdlib>
#include <random>
#include <unordered_map>

#include "capture/flat_map.h"

namespace {

using torchlight::capture::FlatMap;
using torchlight::capture::FlatSet;

void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    std::exit(1);
  }
}

struct CollidingHash {  // 4 distinct hashes, all near the end of the table
  size_t operator()(uint64_t key) const noexcept { return size_t(~0ull - (key & 3)); }
};

template <typename Hash>
void Random(uint64_t seed, uint64_t key_range, int steps) {
  FlatMap<uint64_t, uint64_t, Hash> flat;
  std::unordered_map<uint64_t, uint64_t> ref;
  std::mt19937_64 rng(seed);
  for (int step = 0; step < steps; ++step) {
    const uint64_t key = rng() % key_range;
    switch (rng() % 4) {
      case 0:
      case 1: {
        const uint64_t value = rng();
        flat[key] = value;
        ref[key] = value;
        break;
      }
      case 2:
        Check(flat.erase(key) == (ref.erase(key) == 1), "erase result");
        break;
      default: {
        const uint64_t* v = flat.find(key);
        auto it = ref.find(key);
        Check((v != nullptr) == (it != ref.end()), "presence");
        if (v) Check(*v == it->second, "value");
      }
    }
    Check(flat.size() == ref.size(), "size");
  }
  // Every entry still reachable after all the erases, and nothing extra.
  for (const auto& [key, value] : ref) {
    const uint64_t* v = flat.find(key);
    Check(v && *v == value, "final contents");
  }
  size_t seen = 0;
  flat.ForEach([&](uint64_t key, uint64_t value) {
    Check(ref.count(key) && ref.at(key) == value, "iteration");
    ++seen;
  });
  Check(seen == ref.size(), "iteration count");
}

}  // namespace

int main() {
  for (uint64_t seed = 1; seed <= 20; ++seed) {
    Random<torchlight::capture::IntegerHash>(seed, 64, 20000);
    Random<torchlight::capture::IntegerHash>(seed, 100000, 20000);
    Random<CollidingHash>(seed, 48, 5000);
  }
  // Insert reports a new key; operator[] default-constructs; clear empties.
  FlatMap<uint32_t, int> m;
  Check(m.Insert(7).second && !m.Insert(7).second, "insert reports a new key");
  m[8] += 5;
  Check(*m.find(8) == 5 && m.size() == 2, "default-constructed then updated");
  m.clear();
  Check(m.empty() && !m.find(7), "clear");
  FlatSet<uint64_t> s;
  Check(s.insert(3) && !s.insert(3) && s.contains(3) && s.erase(3) && !s.contains(3), "set");
  std::printf("flat map test: ok\n");
  return 0;
}
