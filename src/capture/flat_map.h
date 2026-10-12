// A hash map for the producer's per-draw lookups: open addressing with linear probing in one array
// (no node per element, no division: a power-of-two capacity), at most half full, and deletion by
// shifting the following entries back (no tombstones). std::unordered_map's node walk and prime
// modulo cost about 3 % of the guest's render thread in the town square profile (2026-10-08).
//
// For keys that are cheap to copy and compare (integers, small structs). Pointers to values stay
// valid until the next insertion or erase. Iteration order is unspecified.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace torchlight::capture {

// A 64-bit mix (splitmix64's finalizer): integer keys spread over the low bits the mask keeps.
inline uint64_t MixHash(uint64_t x) {
  x ^= x >> 30;
  x *= 0xBF58476D1CE4E5B9ull;
  x ^= x >> 27;
  x *= 0x94D049BB133111EBull;
  return x ^ (x >> 31);
}

struct IntegerHash {
  size_t operator()(uint64_t key) const noexcept { return size_t(MixHash(key)); }
};

template <typename Key, typename Value, typename Hash = IntegerHash>
class FlatMap {
 public:
  // The value at `key`, or nullptr.
  Value* find(const Key& key) {
    if (slots_.empty()) return nullptr;
    for (size_t i = Home(key);; i = Next(i)) {
      Slot& s = slots_[i];
      if (!s.used) return nullptr;
      if (s.key == key) return &s.value;
    }
  }
  const Value* find(const Key& key) const { return const_cast<FlatMap*>(this)->find(key); }
  bool contains(const Key& key) const { return find(key) != nullptr; }

  // The value at `key`, default-constructed if absent.
  Value& operator[](const Key& key) { return Insert(key).first; }
  // Inserts a default value at `key` if absent; the value, and whether it was inserted.
  std::pair<Value&, bool> Insert(const Key& key) {
    if ((size_ + 1) * 2 > slots_.size()) Grow();
    size_t i = Home(key);
    for (;; i = Next(i)) {
      Slot& s = slots_[i];
      if (!s.used) break;
      if (s.key == key) return {s.value, false};
    }
    Slot& s = slots_[i];
    s.used = true;
    s.key = key;
    s.value = Value{};
    ++size_;
    return {s.value, true};
  }

  // Removes `key`; whether it was there. The entries probed after it move back so that every
  // entry stays reachable from its home slot without a gap.
  bool erase(const Key& key) {
    if (slots_.empty()) return false;
    size_t i = Home(key);
    for (;; i = Next(i)) {
      if (!slots_[i].used) return false;
      if (slots_[i].key == key) break;
    }
    for (size_t j = Next(i);; j = Next(j)) {
      Slot& s = slots_[j];
      if (!s.used) break;
      const size_t home = Home(s.key);
      // s may move into the gap at i only if its home is not in the cyclic range (i, j].
      const bool home_after_gap = i <= j ? (home > i && home <= j) : (home > i || home <= j);
      if (home_after_gap) continue;
      slots_[i] = std::move(s);
      i = j;
    }
    slots_[i].used = false;
    slots_[i].value = Value{};
    --size_;
    return true;
  }

  size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }
  void clear() {
    for (Slot& s : slots_) s = Slot{};
    size_ = 0;
  }
  // Calls f(key, value) for every entry, in no particular order.
  template <typename F>
  void ForEach(F&& f) const {
    for (const Slot& s : slots_) {
      if (s.used) f(s.key, s.value);
    }
  }

 private:
  struct Slot {
    Key key{};
    Value value{};
    bool used = false;
  };
  size_t Home(const Key& key) const { return Hash{}(key) & (slots_.size() - 1); }
  size_t Next(size_t i) const { return (i + 1) & (slots_.size() - 1); }
  void Grow() {
    std::vector<Slot> old = std::move(slots_);
    slots_.assign(old.empty() ? 16 : old.size() * 2, Slot{});
    size_ = 0;
    for (Slot& s : old) {
      if (s.used) Insert(s.key).first = std::move(s.value);
    }
  }

  std::vector<Slot> slots_;
  size_t size_ = 0;
};

// A set on the same table.
template <typename Key, typename Hash = IntegerHash>
class FlatSet {
 public:
  bool contains(const Key& key) const { return map_.contains(key); }
  // Whether it was inserted (absent before).
  bool insert(const Key& key) { return map_.Insert(key).second; }
  bool erase(const Key& key) { return map_.erase(key); }
  size_t size() const { return map_.size(); }
  void clear() { map_.clear(); }

 private:
  struct Empty {};
  FlatMap<Key, Empty, Hash> map_;
};

}  // namespace torchlight::capture
