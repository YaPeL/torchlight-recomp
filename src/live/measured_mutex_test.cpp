// MeasuredMutex: counts acquisitions, the contended ones and the wait/hold times, per name.

#include "live/measured_mutex.h"

#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <thread>

using torchlight::live::MeasuredMutex;
using torchlight::live::MutexSummary;
using torchlight::live::TakeMutexSummaries;

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}
MutexSummary Find(const char* name) {
  for (auto& s : TakeMutexSummaries())
    if (s.name == name) return s;
  return {name, 0, 0, 0, 0};
}
}  // namespace

int main() {
  using namespace std::chrono_literals;
  {
    MeasuredMutex m("uncontended");
    for (int i = 0; i < 3; ++i) std::lock_guard<MeasuredMutex> lock(m);
    MutexSummary s = Find("uncontended");
    Check(s.acquisitions == 3, "three acquisitions");
    Check(s.contended == 0 && s.wait_ns == 0, "no contention without a second thread");
    Check(Find("uncontended").acquisitions == 0, "counts restart after being taken");
  }
  {
    // Held for 50 ms by one thread while another waits for it.
    MeasuredMutex m("contended");
    std::mutex gate_mutex;
    std::condition_variable gate;
    bool held = false;
    std::thread holder([&] {
      std::lock_guard<MeasuredMutex> lock(m);
      {
        std::lock_guard<std::mutex> g(gate_mutex);
        held = true;
      }
      gate.notify_one();
      std::this_thread::sleep_for(50ms);
    });
    {
      std::unique_lock<std::mutex> g(gate_mutex);
      gate.wait(g, [&] { return held; });
    }
    { std::lock_guard<MeasuredMutex> lock(m); }
    holder.join();
    MutexSummary s = Find("contended");
    Check(s.acquisitions == 2, "two acquisitions");
    Check(s.contended == 1, "one contended acquisition");
    Check(s.wait_ns >= 30'000'000, "waited most of the holder's 50 ms");
    Check(s.hold_ns >= 50'000'000, "held at least 50 ms in total");
  }
  {
    // Same name: instances add up.
    MeasuredMutex a("shared"), b("shared");
    { std::lock_guard<MeasuredMutex> lock(a); }
    { std::lock_guard<MeasuredMutex> lock(b); }
    Check(Find("shared").acquisitions == 2, "instances with one name add up");
  }
  if (failures) return EXIT_FAILURE;
  std::printf("measured_mutex_test: ok\n");
  return EXIT_SUCCESS;
}
