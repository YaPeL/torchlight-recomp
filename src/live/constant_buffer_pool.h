// The vectors of the live mode's SetConstants commands, reused instead of allocated by the guest's
// render thread and freed by the backend's for every command (on the POCO X7 the allocator was
// 2.7 % of the guest thread in a fight, half of it under ReadConstants).
//
// A buffer goes back to the producer only after the consumer is done with the frame that held it:
// the thread that frees consumed frames (frame_reclaimer.h, or the consumer itself when it frees
// them) returns a frame's buffers, emptied, once the backend has executed the frame
// (ReturnFrame), and the guest thread takes them at its next frame cut (Refill). However many frames
// the guest runs ahead, and whatever the queue drops (a dropped frame's state commands travel in the
// next frame and come back with it), no buffer is reused while a frame that holds it is queued or
// being executed. The pool is bounded: past its limits, or for oversized buffers, the consumer
// frees as before.
//
// The producer side is per thread (no lock): a thread that never refills just allocates.

#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#include "commands/types.h"

namespace torchlight::live {

struct LiveFrame;

class ConstantBufferPool {
 public:
  static ConstantBufferPool& Get();

  // Producer (guest render thread): an empty vector, with capacity when one came back.
  std::vector<uint32_t> TakeData();
  std::vector<commands::ConstantRange> TakeRanges();
  std::vector<commands::AutoConstant> TakeAutos();
  // Producer, at the frame cut: takes what the consumer returned.
  void Refill();

  // Consumer, after executing `frame`: moves the vectors of its SetConstants into the pool, empty.
  void ReturnFrame(LiveFrame& frame);

  struct Counts {
    uint64_t reused = 0, allocated = 0, returned = 0, freed = 0;
  };
  // Since the previous call (they restart from zero).
  Counts TakeCounts();

  // Limits: buffers kept, and the largest kept (larger ones are freed; a recycled vector keeps its
  // capacity, so without a cap every buffer would grow to the largest range it ever held, such as a
  // skinning palette). At most ~8 MiB of data buffers and a few hundred KiB of lists.
  static constexpr size_t kMaxData = 8192, kMaxRanges = 4096, kMaxAutos = 4096;
  static constexpr size_t kMaxDataCapacity = 256, kMaxListCapacity = 64;

 private:
  struct Spares {
    std::vector<std::vector<uint32_t>> data;
    std::vector<std::vector<commands::ConstantRange>> ranges;
    std::vector<std::vector<commands::AutoConstant>> autos;
  };
  static Spares& Local();  // the calling producer thread's

  std::mutex mutex_;
  Spares returned_;  // guarded by mutex_
  Counts counts_;    // guarded by mutex_ (the producer's are added at Refill)
  static thread_local Counts local_counts_;
};

}  // namespace torchlight::live
