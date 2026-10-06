// A mutex that measures its own contention, for the live mode's periodic summary.
//
// Same use as std::mutex (lock_guard, unique_lock, condition_variable_any). Per name it counts
// acquisitions, the ones that had to wait (the mutex was held), the time spent waiting and the
// time held. Instances with the same name add up.

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace torchlight::live {

struct MutexStats {
  std::atomic<uint64_t> acquisitions{0}, contended{0}, wait_ns{0}, hold_ns{0};
};

class MeasuredMutex {
 public:
  explicit MeasuredMutex(const char* name);
  MeasuredMutex(const MeasuredMutex&) = delete;
  MeasuredMutex& operator=(const MeasuredMutex&) = delete;

  void lock() {
    if (!mutex_.try_lock()) {
      auto start = Now();
      mutex_.lock();
      stats_->contended.fetch_add(1, std::memory_order_relaxed);
      stats_->wait_ns.fetch_add(Now() - start, std::memory_order_relaxed);
    }
    Acquired();
  }
  bool try_lock() {
    if (!mutex_.try_lock()) return false;
    Acquired();
    return true;
  }
  void unlock() {
    stats_->hold_ns.fetch_add(Now() - held_since_, std::memory_order_relaxed);
    mutex_.unlock();
  }

 private:
  static uint64_t Now() {
    return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now().time_since_epoch())
                        .count());
  }
  void Acquired() {
    held_since_ = Now();
    stats_->acquisitions.fetch_add(1, std::memory_order_relaxed);
  }

  std::mutex mutex_;
  MutexStats* stats_;
  uint64_t held_since_ = 0;  // written and read by the holder only
};

struct MutexSummary {
  std::string name;
  uint64_t acquisitions, contended, wait_ns, hold_ns;
};
// Every name's counts since the previous call (they restart from zero), in name order.
std::vector<MutexSummary> TakeMutexSummaries();

}  // namespace torchlight::live
