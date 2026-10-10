// Frames of the live mode, cut at the guest's swap, handed from the guest render thread to the
// backend thread.
//
// By default the producer never waits: with the queue full, the oldest queued frame is dropped.
// Dropping keeps continuity: its state commands and resources are carried into the next frame, only
// its draws and clears are discarded (counted).
//
// With backpressure (--live_backpressure) the producer waits at Push while a frame is queued that
// the consumer has not taken yet, so the guest makes no frame that would be dropped; after `cap`
// (a consumer that stopped: a minimized window, a lost device) it pushes anyway, dropping as
// above.

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

  bool empty() const;
  // Moves `older`'s state and resources in front of this frame's; its draws and clears are
  // discarded.
  void AbsorbDropped(LiveFrame&& older);
};

class FrameQueue {
 public:
  explicit FrameQueue(size_t capacity = 2) : capacity_(capacity) {}

  // Before the producer starts. Without it, Push never blocks.
  void SetBackpressure(std::chrono::milliseconds cap);
  // Producer: with backpressure, blocks up to the cap while a queued frame is not taken.
  void Push(LiveFrame frame);
  // Consumer: waits up to `timeout` for a frame.
  std::optional<LiveFrame> Pop(std::chrono::milliseconds timeout);
  void Close();  // wakes the consumer; Pop returns nullopt once empty

  uint64_t pushed() const;
  uint64_t dropped() const;
  // Backpressure: the producer's waits, their total time and how many reached the cap.
  struct Waits {
    uint64_t count = 0, timeouts = 0;
    double ms = 0;
  };
  Waits waits() const;

 private:
  size_t capacity_;
  mutable MeasuredMutex mutex_{"frame queue"};
  std::condition_variable_any ready_;
  std::condition_variable_any taken_;  // the consumer took a frame
  std::optional<std::chrono::milliseconds> backpressure_;
  Waits waits_;
  std::deque<LiveFrame> frames_;
  bool closed_ = false;
  uint64_t pushed_ = 0, dropped_ = 0;
};

}  // namespace torchlight::live
