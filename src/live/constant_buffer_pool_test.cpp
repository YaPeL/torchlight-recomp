// The constant buffer pool (constant_buffer_pool.h): buffers come back only from frames the consumer
// has finished, with the consumer late on purpose (the guest runs ahead, the queue drops frames),
// and every value of every executed frame is the one its producer wrote.

#include <chrono>
#include <cstdio>
#include <thread>
#include <variant>

#include "live/constant_buffer_pool.h"
#include "live/frame_queue.h"
#include "live/frame_reclaimer.h"

using namespace torchlight;
using namespace torchlight::live;

namespace {

int failures = 0;

void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

uint32_t Value(uint32_t frame, uint32_t command, uint32_t range, uint32_t i) {
  return frame * 1000003u ^ command * 7919u ^ range * 104729u ^ i * 31u;
}

constexpr uint32_t kCommands = 40, kRanges = 3, kValues = 16;

// A frame's constants, from the pool as ReadConstants takes them.
LiveFrame MakeFrame(uint32_t f) {
  auto& pool = ConstantBufferPool::Get();
  LiveFrame frame;
  frame.swap = f;
  for (uint32_t k = 0; k < kCommands; ++k) {
    commands::SetConstants c;
    c.parameters_guest_address = k;
    c.floats = pool.TakeRanges();
    for (uint32_t r = 0; r < kRanges; ++r) {
      commands::ConstantRange range;
      range.logical_index = r;
      range.physical_index = f;  // the frame that wrote it
      range.data = pool.TakeData();
      for (uint32_t i = 0; i < kValues; ++i) range.data.push_back(Value(f, k, r, i));
      c.floats.push_back(std::move(range));
    }
    c.autos = pool.TakeAutos();
    commands::AutoConstant a;
    a.raw_type = f;
    a.physical_index = k;
    c.autos.push_back(a);
    frame.commands.push_back({0, std::move(c)});
  }
  return frame;
}

// Every value as its producer wrote it.
bool Intact(const LiveFrame& frame) {
  for (const auto& command : frame.commands) {
    const auto* c = std::get_if<commands::SetConstants>(&command.payload);
    if (!c) continue;
    const uint32_t k = c->parameters_guest_address;
    if (c->floats.size() != kRanges || c->autos.size() != 1 || c->autos[0].physical_index != k)
      return false;
    for (const auto& range : c->floats) {
      if (range.data.size() != kValues) return false;
      for (uint32_t i = 0; i < kValues; ++i) {
        if (range.data[i] != Value(range.physical_index, k, range.logical_index, i)) return false;
      }
    }
  }
  return true;
}

void TestSingleThread() {
  auto& pool = ConstantBufferPool::Get();
  pool.TakeCounts();
  LiveFrame frame = MakeFrame(1);
  pool.ReturnFrame(frame);
  pool.Refill();
  auto counts = pool.TakeCounts();
  Check(counts.reused == 0, "nothing to reuse before a frame came back");
  Check(counts.returned == kCommands * (kRanges + 2), "every buffer of the frame came back");
  LiveFrame again = MakeFrame(2);
  pool.Refill();
  counts = pool.TakeCounts();
  Check(counts.reused == kCommands * (kRanges + 2) && counts.allocated == 0,
        "the second frame took only returned buffers");
  Check(Intact(again), "and holds its own values");
  // A frame that has not come back keeps its buffers.
  LiveFrame held = MakeFrame(3);
  pool.Refill();
  Check(Intact(held) && Intact(again), "frames not returned are untouched");
  pool.ReturnFrame(again);
  pool.ReturnFrame(held);
  pool.Refill();
  pool.TakeCounts();
  // Oversized buffers are freed, not kept.
  LiveFrame big;
  commands::SetConstants c;
  c.floats.push_back({});
  c.floats[0].data.resize(ConstantBufferPool::kMaxDataCapacity + 1);
  big.commands.push_back({0, std::move(c)});
  pool.ReturnFrame(big);
  counts = pool.TakeCounts();
  Check(counts.freed == 1, "an oversized buffer is freed");
}

// The consumer late: the producer runs ahead, the queue (3) drops frames into the next one, and
// executed frames go through a real reclaimer.
void TestLateConsumer() {
  auto& pool = ConstantBufferPool::Get();
  pool.TakeCounts();
  FrameQueue queue(3);
  constexpr uint32_t kFrames = 300;
  bool all_intact = true;
  uint32_t consumed = 0;
  std::thread consumer([&] {
    FrameReclaimer reclaimer;
    while (auto frame = queue.Pop(std::chrono::milliseconds(500))) {
      std::this_thread::sleep_for(std::chrono::microseconds(800));  // the backend behind
      all_intact = all_intact && Intact(*frame);
      ++consumed;
      reclaimer.Free(std::move(*frame));
    }
  });
  for (uint32_t f = 0; f < kFrames; ++f) {
    queue.Push(MakeFrame(f));
    pool.Refill();  // the cut
    std::this_thread::sleep_for(std::chrono::microseconds(200));
  }
  queue.Close();
  consumer.join();
  pool.Refill();
  const auto counts = pool.TakeCounts();
  std::printf("late consumer: %u frames, %u executed, %llu dropped; buffers %llu reused, %llu "
              "allocated, %llu returned\n",
              kFrames, consumed, (unsigned long long)queue.dropped(),
              (unsigned long long)counts.reused, (unsigned long long)counts.allocated,
              (unsigned long long)counts.returned);
  Check(all_intact, "every executed frame holds the values its producer wrote");
  Check(queue.dropped() > 0, "the queue dropped frames (the consumer was late)");
  Check(counts.reused > counts.allocated, "most buffers were reused");
}

}  // namespace

int main() {
  TestSingleThread();
  TestLateConsumer();
  std::printf("constant_buffer_pool_test: %s\n", failures ? "FAILED" : "OK");
  return failures ? 1 : 0;
}
