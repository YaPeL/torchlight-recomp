#include "live/frame_queue.h"

#include <algorithm>

#include <iterator>
#include <variant>

namespace torchlight::live {

namespace {
template <typename T>
void Prepend(std::vector<T>& to, std::vector<T>&& from) {
  from.insert(from.end(), std::make_move_iterator(to.begin()), std::make_move_iterator(to.end()));
  to = std::move(from);
}
}  // namespace

bool LiveFrame::empty() const {
  return commands.empty() && vertex_buffers.empty() && index_buffers.empty() &&
         declarations.empty() && textures.empty() && programs.empty() &&
         program_sources.empty() && destroyed.empty();
}

void LiveFrame::AbsorbDropped(LiveFrame&& older) {
  std::vector<commands::Command> kept;
  kept.reserve(older.commands.size());
  for (auto& c : older.commands) {
    if (std::holds_alternative<commands::Draw>(c.payload) ||
        std::holds_alternative<commands::Clear>(c.payload)) {
      continue;
    }
    kept.push_back(std::move(c));
  }
  Prepend(commands, std::move(kept));
  Prepend(vertex_buffers, std::move(older.vertex_buffers));
  Prepend(index_buffers, std::move(older.index_buffers));
  Prepend(declarations, std::move(older.declarations));
  Prepend(textures, std::move(older.textures));
  Prepend(programs, std::move(older.programs));
  Prepend(program_sources, std::move(older.program_sources));
  Prepend(destroyed, std::move(older.destroyed));
  Prepend(contents, std::move(older.contents));
  dropped_before += older.dropped_before + 1;
  producer.worst_dropped_guest_ms =
      std::max({producer.worst_dropped_guest_ms, older.producer.guest_ms,
                older.producer.worst_dropped_guest_ms});
  producer.snapshot_copies += older.producer.snapshot_copies;
  producer.snapshot_bytes += older.producer.snapshot_bytes;
}

void FrameQueue::SetBackpressure(std::chrono::milliseconds cap) {
  std::lock_guard<MeasuredMutex> lock(mutex_);
  backpressure_ = cap;
}

void FrameQueue::Push(LiveFrame frame) {
  {
    std::unique_lock<MeasuredMutex> lock(mutex_);
    if (backpressure_ && !frames_.empty() && !closed_) {
      const auto start = std::chrono::steady_clock::now();
      const bool taken = taken_.wait_for(lock, *backpressure_, [&] { return frames_.empty() || closed_; });
      ++waits_.count;
      if (!taken) ++waits_.timeouts;
      waits_.ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    }
    ++pushed_;
    frames_.push_back(std::move(frame));
    while (frames_.size() > capacity_) {
      LiveFrame oldest = std::move(frames_.front());
      frames_.pop_front();
      frames_.front().AbsorbDropped(std::move(oldest));
      ++dropped_;
    }
  }
  ready_.notify_one();
}

std::optional<LiveFrame> FrameQueue::Pop(std::chrono::milliseconds timeout) {
  std::unique_lock<MeasuredMutex> lock(mutex_);
  ready_.wait_for(lock, timeout, [&] { return !frames_.empty() || closed_; });
  if (frames_.empty()) return std::nullopt;
  LiveFrame f = std::move(frames_.front());
  frames_.pop_front();
  lock.unlock();
  taken_.notify_one();
  return f;
}

void FrameQueue::Close() {
  {
    std::lock_guard<MeasuredMutex> lock(mutex_);
    closed_ = true;
  }
  ready_.notify_all();
  taken_.notify_all();
}

uint64_t FrameQueue::pushed() const {
  std::lock_guard<MeasuredMutex> lock(mutex_);
  return pushed_;
}

uint64_t FrameQueue::dropped() const {
  std::lock_guard<MeasuredMutex> lock(mutex_);
  return dropped_;
}

}  // namespace torchlight::live

namespace torchlight::live {

FrameQueue::Waits FrameQueue::waits() const {
  std::lock_guard<MeasuredMutex> lock(mutex_);
  return waits_;
}

}  // namespace torchlight::live
