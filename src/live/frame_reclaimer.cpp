#include "live/frame_reclaimer.h"

#include <utility>

namespace torchlight::live {

FrameReclaimer::FrameReclaimer() : thread_([this] { Run(); }) {}

FrameReclaimer::~FrameReclaimer() {
  {
    std::lock_guard lock(mutex_);
    stop_ = true;
  }
  ready_.notify_one();
  thread_.join();
}

void FrameReclaimer::Free(LiveFrame&& frame) {
  {
    std::lock_guard lock(mutex_);
    if (pending_.size() < kMaxPending) {
      pending_.push_back(std::move(frame));
      ready_.notify_one();
      return;
    }
    ++freed_inline_;
  }
  LiveFrame freed = std::move(frame);  // the backlog is full: freed here, outside the lock
}

void FrameReclaimer::Run() {
  std::unique_lock lock(mutex_);
  for (;;) {
    ready_.wait(lock, [&] { return stop_ || !pending_.empty(); });
    if (pending_.empty()) return;  // stopping, nothing left
    LiveFrame frame = std::move(pending_.front());
    pending_.pop_front();
    lock.unlock();
    { LiveFrame freed = std::move(frame); }
    lock.lock();
    ++freed_behind_;
  }
}

size_t FrameReclaimer::freed_behind() const {
  std::lock_guard lock(mutex_);
  return freed_behind_;
}

size_t FrameReclaimer::freed_inline() const {
  std::lock_guard lock(mutex_);
  return freed_inline_;
}

}  // namespace torchlight::live
