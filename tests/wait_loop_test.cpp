// Standalone SDK regression/CPU test; link against either runtime build.
#ifdef NDEBUG
#error "This standalone test requires assertions enabled."
#endif
#include <rex/thread.h>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <string_view>
#include <thread>
#include <time.h>

using namespace std::chrono_literals;
using rex::thread::Event;
using rex::thread::Semaphore;
using rex::thread::Wait;
using rex::thread::WaitAny;
using rex::thread::WaitAll;
using rex::thread::WaitResult;

double cpu_seconds() {
  timespec time{};
  assert(clock_gettime(CLOCK_THREAD_CPUTIME_ID, &time) == 0);
  return time.tv_sec + time.tv_nsec * 1e-9;
}

int main(int argc, char** argv) {
  // Zero timeout, ordering, auto-reset consumption and manual-reset persistence.
  auto first = Event::CreateAutoResetEvent(false);
  auto second = Event::CreateAutoResetEvent(true);
  rex::thread::WaitHandle* handles[]{first.get(), second.get()};
  auto result = WaitAny(handles, 2, false, 0ms);
  assert(result.first == WaitResult::kSuccess && result.second == 1);
  assert(WaitAny(handles, 2, false, 0ms).first == WaitResult::kTimeout);
  first->Set(); second->Set();
  assert(WaitAny(handles, 2, false, 0ms).second == 0);
  assert(WaitAny(handles, 2, false, 0ms).second == 1);
  auto manual = Event::CreateManualResetEvent(true);
  rex::thread::WaitHandle* mixed[]{first.get(), manual.get()};
  assert(WaitAny(mixed, 2, false, 0ms).second == 1);
  assert(WaitAny(mixed, 2, false, 0ms).second == 1);
  manual->Reset();

  // A failed wait-all must not consume the already-signaled auto-reset event.
  first->Set();
  assert(WaitAll(handles, 2, false, 0ms) == WaitResult::kTimeout);
  assert(Wait(first.get(), false, 0ms) == WaitResult::kSuccess);
  first->Set(); second->Set();
  assert(WaitAll(handles, 2, true, 100ms) == WaitResult::kSuccess);
  assert(WaitAny(handles, 2, false, 0ms).first == WaitResult::kTimeout);

  // Finite and infinite waits still respond to a later signal.
  for (bool alertable : {false, true}) {
    for (auto timeout : {100ms, std::chrono::milliseconds::max()}) {
      std::thread signaler([&] { std::this_thread::sleep_for(15ms); second->Set(); });
      result = WaitAny(handles, 2, alertable, timeout);
      signaler.join();
      assert(result.first == WaitResult::kSuccess && result.second == 1);
    }
  }
  auto semaphore = Semaphore::Create(2, 2);
  rex::thread::WaitHandle* sem_handles[]{first.get(), semaphore.get()};
  assert(WaitAny(sem_handles, 2, true, 100ms).second == 1);
  assert(WaitAny(sem_handles, 2, true, 100ms).second == 1);
  assert(WaitAny(sem_handles, 2, false, 0ms).first == WaitResult::kTimeout);

  // An alertable wait must dispatch queued callbacks and return kUserCallback.
  std::atomic<bool> started{}, called{};
  std::atomic<WaitResult> callback_result{WaitResult::kFailed};
  auto waiter = rex::thread::Thread::Create({}, [&] {
    started.store(true);
    callback_result.store(WaitAny(handles, 2, true, 2s).first);
  });
  while (!started.load()) std::this_thread::yield();
  waiter->QueueUserCallback([&] { called.store(true); });
  assert(Wait(waiter.get(), false, 2s) == WaitResult::kSuccess);
  assert(called.load() && callback_result.load() == WaitResult::kUserCallback);

  // Representative audio wait: multiple idle objects, 1 ms alertable slices.
  // Wall timeout semantics are checked independently of the CPU regression.
  const auto began = std::chrono::steady_clock::now();
  const double cpu_start = cpu_seconds();
  assert(WaitAny(handles, 2, true, 300ms).first == WaitResult::kTimeout);
  const double cpu = cpu_seconds() - cpu_start;
  const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count();
  assert(wall >= .29 && wall < 2.0);
  std::printf("wait_semantics=PASS wall_seconds=%.6f cpu_seconds=%.6f cpu_percent=%.3f\n",
              wall, cpu, 100*cpu/wall);
  if (argc > 1 && std::string_view(argv[1]) == "--require-low-cpu") {
    assert(cpu < wall*.25);  // Old truncation path should fail this check.
  }
}
