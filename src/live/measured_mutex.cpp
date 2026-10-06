#include "live/measured_mutex.h"

#include <map>
#include <memory>

namespace torchlight::live {

namespace {
struct Registry {
  std::mutex mutex;
  std::map<std::string, std::unique_ptr<MutexStats>> stats;  // never erased: instances point here
};
Registry& GetRegistry() {
  static Registry* registry = new Registry();  // outlives every static MeasuredMutex
  return *registry;
}
}  // namespace

MeasuredMutex::MeasuredMutex(const char* name) {
  Registry& r = GetRegistry();
  std::lock_guard<std::mutex> lock(r.mutex);
  auto& stats = r.stats[name];
  if (!stats) stats = std::make_unique<MutexStats>();
  stats_ = stats.get();
}

std::vector<MutexSummary> TakeMutexSummaries() {
  Registry& r = GetRegistry();
  std::lock_guard<std::mutex> lock(r.mutex);
  std::vector<MutexSummary> out;
  for (auto& [name, s] : r.stats) {
    out.push_back({name, s->acquisitions.exchange(0), s->contended.exchange(0),
                   s->wait_ns.exchange(0), s->hold_ns.exchange(0)});
  }
  return out;
}

}  // namespace torchlight::live
