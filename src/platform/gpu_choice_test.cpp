// Tests for the GPU choice rules (platform.h SelectableGpus, GpuEnvironment) on machines described
// by hand, plus a look at this machine's GPUs.

#include <cstdio>
#include <cstdlib>

#include "platform/platform.h"

namespace {

using torchlight::platform::Gpu;
using torchlight::platform::GpuEnvironment;
using torchlight::platform::SelectableGpus;

void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    std::exit(1);
  }
}

void TestIntelNvidia() {
  const std::vector<Gpu> gpus = {{"0000:00:02.0", "Intel UHD Graphics 630", "i915", true},
                                 {"0000:01:00.0", "NVIDIA GeForce GTX 1050 Ti", "nvidia", false}};
  Check(SelectableGpus(gpus).size() == 2, "Intel + NVIDIA: both");
  Check(GpuEnvironment(gpus, "0000:00:02.0").empty(), "boot GPU: nothing to set");
  const auto nvidia = GpuEnvironment(gpus, "0000:01:00.0");
  Check(nvidia.size() == 1 && nvidia[0].first == "__NV_PRIME_RENDER_OFFLOAD" &&
            nvidia[0].second == "1",
        "NVIDIA: PRIME render offload, never DRI_PRIME");
  Check(GpuEnvironment(gpus, "0000:09:00.0").empty(), "unknown id: nothing");
}

void TestIntelAmd() {
  const std::vector<Gpu> gpus = {{"0000:00:02.0", "Intel", "i915", true},
                                 {"0000:03:00.0", "AMD", "amdgpu", false}};
  const auto amd = GpuEnvironment(gpus, "0000:03:00.0");
  Check(amd.size() == 1 && amd[0].first == "DRI_PRIME" && amd[0].second == "pci-0000_03_00_0",
        "Mesa to Mesa: DRI_PRIME with Mesa's slot form");
}

void TestNvidiaBoot() {
  const std::vector<Gpu> gpus = {{"0000:01:00.0", "NVIDIA", "nvidia", true},
                                 {"0000:00:02.0", "Intel", "i915", false}};
  Check(SelectableGpus(gpus).size() == 1, "NVIDIA boot: the Mesa GPU cannot be guaranteed");
  Check(GpuEnvironment(gpus, "0000:00:02.0").empty(), "and gets nothing set");
}

void TestSingle() {
  const std::vector<Gpu> gpus = {{"0000:04:00.0", "AMD Custom GPU 0405", "amdgpu", true}};
  Check(SelectableGpus(gpus).size() == 1, "one GPU (Steam Deck)");
}

}  // namespace

int main() {
  TestIntelNvidia();
  TestIntelAmd();
  TestNvidiaBoot();
  TestSingle();
  for (const Gpu& gpu : torchlight::platform::Gpus()) {
    std::printf("  %s %s (%s)%s\n", gpu.id.c_str(), gpu.name.c_str(), gpu.driver.c_str(),
                gpu.boot ? " boot" : "");
  }
  std::printf("gpu_choice_test: ok\n");
  return 0;
}
