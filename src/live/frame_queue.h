// Frames of the live mode, cut at the guest's swap, handed from the guest render thread to the
// backend thread.
//
// The producer never waits: with the queue full, the oldest queued frame is dropped. Dropping
// keeps continuity: its state commands and resources are carried into the next frame, only its
// draws and clears are discarded (counted). The merge happens on the consumer's thread, after it
// pops the frame: the producer only moves the dropped frame aside (it is the guest's render
// thread, where moving a frame's thousands of commands and freeing its draws cost ~0.5 M cycles a
// frame on the POCO with frames dropping). The merged frame keeps what the dropped frames
// discarded until it is itself freed (on the reclaimer's thread). Past kMaxAbsorbedPending drops
// into one frame (a stalled consumer) the oldest is merged and freed on the producer, as before.

#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <vector>

#include "commands/types.h"
#include "live/measured_mutex.h"
#include "live/snapshot_store.h"

namespace torchlight::live {

// Dropped frames a queued frame holds unmerged; past it the oldest is merged at push (bounds the
// memory while the consumer is stalled, a load for instance).
constexpr size_t kMaxAbsorbedPending = 4;

struct LiveFrame {
  uint64_t swap = 0;
  std::vector<commands::Command> commands;
  // Resources first seen or changed since the previous frame. Applied before the commands.
  std::vector<commands::VertexBufferDesc> vertex_buffers;
  std::vector<commands::IndexBufferDesc> index_buffers;
  std::vector<commands::VertexDeclarationContent> declarations;
  std::vector<commands::TextureDesc> textures;
  std::vector<commands::ProgramDesc> programs;
  std::vector<commands::ProgramSource> program_sources;
  std::vector<commands::ResourceId> destroyed;
  // Content the commands and textures refer to, with its resource; holding it keeps it alive
  // until rendered.
  struct Content {
    commands::ResourceId id;
    SnapshotStore::Content content;
  };
  std::vector<Content> contents;
  uint32_t dropped_before = 0;  // frames dropped since the previous frame that was rendered
  std::chrono::steady_clock::time_point cut;  // when the guest swapped
  // The guest side of the frame, for the slow frame report (SlowFrameDetector).
  struct Producer {
    double guest_ms = 0;               // swap to swap
    double worst_dropped_guest_ms = 0; // the slowest of the frames dropped into this one
    double recording_ms = -1;          // our recording on the guest threads; -1 when not timed
    uint32_t snapshot_copies = 0;      // content copied from guest memory (dropped frames' too)
    uint64_t snapshot_bytes = 0;
  };
  Producer producer;

  // Frames dropped into this one and not merged yet, oldest first: merged at Pop, or by the caller
  // after Pop(..., false); past kMaxAbsorbedPending the oldest is merged at Push.
  std::vector<LiveFrame> absorbed;
  // What merged frames left behind (their draws and clears), freed with this frame.
  std::vector<LiveFrame> discarded;

  bool empty() const;
  // Moves `older`'s state and resources in front of this frame's; its draws and clears are
  // discarded (they stay in `older`).
  void AbsorbDropped(LiveFrame&& older);
  // Queues `older` (and what was dropped into it) to be merged by MergeAbsorbed (FrameQueue::Push).
  void QueueDropped(LiveFrame&& older);
  // Merges every queued dropped frame, as AbsorbDropped on each from the newest back; their
  // leftovers go to `discarded`.
  void MergeAbsorbed();
};

// The frame queue's depth: `chosen` (--native_frame_queue), or automatic for 0. With vsync the
// display takes one frame a refresh and a guest paced at the same rate (its own vblank, not the
// display's) fills any queue: each frame then waits in it a refresh per frame ahead of it
// (measured: about 35-40 ms of the 70 ms from the guest's swap to the screen with two). One keeps
// only the newest. Without vsync the backend presents as fast as it can and a second frame keeps
// it busy while the guest makes the next (one showed fewer presented frames standing still in the
// dungeon). docs/performance-profile.md, vsync latency.
constexpr size_t FrameQueueDepth(size_t chosen, bool vsync) {
  if (chosen) return chosen;
  return vsync ? 1 : 2;
}

class FrameQueue {
 public:
  explicit FrameQueue(size_t capacity = 2) : capacity_(capacity) {}

  // How many frames wait for the backend (at least one); any thread, applied at the next push.
  void SetCapacity(size_t capacity);

  // Producer: never blocks.
  void Push(LiveFrame frame);
  // Consumer: waits up to `timeout` for a frame. With `merge` false the frames dropped into it are
  // left in `absorbed` for the caller to merge (MergeAbsorbed) where it times its work.
  std::optional<LiveFrame> Pop(std::chrono::milliseconds timeout, bool merge = true);
  void Close();  // wakes the consumer; Pop returns nullopt once empty

  uint64_t pushed() const;
  uint64_t dropped() const;

 private:
  size_t capacity_;
  mutable MeasuredMutex mutex_{"frame queue"};
  std::condition_variable_any ready_;
  std::deque<LiveFrame> frames_;
  bool closed_ = false;
  uint64_t pushed_ = 0, dropped_ = 0;
};

}  // namespace torchlight::live
