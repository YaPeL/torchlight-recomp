#include "live/constant_buffer_pool.h"

#include <utility>
#include <variant>

#include "live/frame_queue.h"

namespace torchlight::live {

thread_local ConstantBufferPool::Counts ConstantBufferPool::local_counts_;

ConstantBufferPool& ConstantBufferPool::Get() {
  static ConstantBufferPool pool;
  return pool;
}

ConstantBufferPool::Spares& ConstantBufferPool::Local() {
  thread_local Spares spares;
  return spares;
}

namespace {

template <typename T>
std::vector<T> TakeFrom(std::vector<std::vector<T>>& spares, ConstantBufferPool::Counts& counts) {
  if (spares.empty()) {
    ++counts.allocated;
    return {};
  }
  std::vector<T> v = std::move(spares.back());
  spares.pop_back();
  ++counts.reused;
  return v;
}

// Appends `from` to `to` up to `limit` entries; returns how many were left out (freed with `from`).
template <typename T>
size_t Append(std::vector<std::vector<T>>& to, std::vector<std::vector<T>>& from, size_t limit) {
  size_t left = 0;
  for (auto& v : from) {
    if (to.size() < limit) to.push_back(std::move(v));
    else ++left;
  }
  from.clear();
  return left;
}

// Moves entries of `from` to `to` while `to` has room; the rest stays in `from` (nothing is freed:
// the producer calls it, and freeing is the consumer's).
template <typename T>
void MoveWhileRoom(std::vector<std::vector<T>>& to, std::vector<std::vector<T>>& from, size_t limit) {
  while (!from.empty() && to.size() < limit) {
    to.push_back(std::move(from.back()));
    from.pop_back();
  }
}

}  // namespace

std::vector<uint32_t> ConstantBufferPool::TakeData() { return TakeFrom(Local().data, local_counts_); }
std::vector<commands::ConstantRange> ConstantBufferPool::TakeRanges() {
  return TakeFrom(Local().ranges, local_counts_);
}
std::vector<commands::AutoConstant> ConstantBufferPool::TakeAutos() {
  return TakeFrom(Local().autos, local_counts_);
}

void ConstantBufferPool::Refill() {
  Spares& local = Local();
  std::lock_guard lock(mutex_);
  MoveWhileRoom(local.data, returned_.data, kMaxData);
  MoveWhileRoom(local.ranges, returned_.ranges, kMaxRanges);
  MoveWhileRoom(local.autos, returned_.autos, kMaxAutos);
  counts_.reused += local_counts_.reused;
  counts_.allocated += local_counts_.allocated;
  local_counts_ = {};
}

void ConstantBufferPool::ReturnFrame(LiveFrame& frame) {
  // Emptied outside the lock; the lock only appends.
  Spares back;
  size_t freed = 0;
  auto keep_ranges = [&](std::vector<commands::ConstantRange>& ranges) {
    for (auto& r : ranges) {
      if (r.data.capacity() == 0) continue;
      if (r.data.capacity() > kMaxDataCapacity) {
        ++freed;
        continue;
      }
      r.data.clear();
      back.data.push_back(std::move(r.data));
    }
    if (ranges.capacity() == 0) return;
    ranges.clear();
    back.ranges.push_back(std::move(ranges));
  };
  for (auto& command : frame.commands) {
    auto* c = std::get_if<commands::SetConstants>(&command.payload);
    if (!c) continue;
    keep_ranges(c->floats);
    keep_ranges(c->ints);
    if (c->autos.capacity() != 0) {
      c->autos.clear();
      back.autos.push_back(std::move(c->autos));
    }
  }
  const size_t returned = back.data.size() + back.ranges.size() + back.autos.size();
  std::lock_guard lock(mutex_);
  freed += Append(returned_.data, back.data, kMaxData);
  freed += Append(returned_.ranges, back.ranges, kMaxRanges);
  freed += Append(returned_.autos, back.autos, kMaxAutos);
  counts_.returned += returned;
  counts_.freed += freed;
}

ConstantBufferPool::Counts ConstantBufferPool::TakeCounts() {
  std::lock_guard lock(mutex_);
  return std::exchange(counts_, {});
}

}  // namespace torchlight::live
