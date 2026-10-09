// Consumed live frames freed off the backend thread.
//
// A frame holds thousands of commands, its resource descriptions and references to content
// snapshots; freeing it took ~0.2 ms of the backend thread per frame in the town square
// (docs/performance-profile.md). None of that reads guest state or touches the render system, so
// a thread of its own frees them. If it falls kMaxPending frames behind, Free frees on the calling
// thread instead, so memory stays bounded.

#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <thread>

#include "live/frame_queue.h"

namespace torchlight::live {

class FrameReclaimer {
 public:
  static constexpr size_t kMaxPending = 8;

  FrameReclaimer();
  ~FrameReclaimer();  // frees what is pending, then joins
  FrameReclaimer(const FrameReclaimer&) = delete;
  FrameReclaimer& operator=(const FrameReclaimer&) = delete;

  void Free(LiveFrame&& frame);

  // Frames freed so far by the reclaimer's thread and by the caller (backlog full).
  size_t freed_behind() const;
  size_t freed_inline() const;

 private:
  void Run();

  mutable std::mutex mutex_;
  std::condition_variable ready_;
  std::deque<LiveFrame> pending_;
  bool stop_ = false;
  size_t freed_behind_ = 0, freed_inline_ = 0;
  std::thread thread_;
};

}  // namespace torchlight::live
