// Tests for the GPU choice from DXGI's adapters (gpu_adapters.h), on made-up machines.

#include "platform/gpu_adapters.h"

#include <cstdio>
#include <cstdlib>

namespace {

using namespace torchlight::platform::gpu_adapters;

void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    std::exit(1);
  }
}

const Adapter kIntel{"Intel(R) UHD Graphics 630", 0x8086, 0x3e9b, 0x09261028, 0x02, false, 11};
const Adapter kNvidia{"NVIDIA GeForce RTX 3060 Laptop GPU", 0x10de, 0x2520, 0x09261028, 0xa1,
                      false, 22};
const Adapter kWarp{"Microsoft Basic Render Driver", 0x1414, 0x008c, 0, 0, true, 33};

void TestHybridLaptop() {
  // EnumAdapters1 puts the integrated GPU first (it drives the panel); the high performance
  // preference puts the dedicated one first.
  const std::vector<Adapter> adapters = {kIntel, kNvidia, kWarp};
  const std::vector<Gpu> gpus = ListGpus(adapters, {22, 11, 33}, false);
  Check(gpus.size() == 2, "two hardware GPUs, WARP left out");
  Check(gpus[0].name == kNvidia.description && gpus[1].name == kIntel.description,
        "listed in the high performance order");
  Check(gpus[0].id == "pci:10de:2520:09261028:a1:0", "id from the PCI ids and the ordinal");
  Check(ListGpus(adapters, {}, false)[0].name == kIntel.description,
        "without a preference order: EnumAdapters1 order");
  const std::vector<std::string> ogre = {"(default)", kIntel.description, kNvidia.description,
                                         "Microsoft Basic Render Driver (software)"};
  Check(RenderingDevice(adapters, gpus[0].id, ogre) == kNvidia.description,
        "the dedicated GPU found in OGRE's list");
  Check(RenderingDevice(adapters, "pci:10de:9999:00000000:00:0", ogre).empty(),
        "unknown id: automatic");
}

void TestTwinGpus() {
  Adapter second = kNvidia;
  second.luid = 23;
  const std::vector<Adapter> adapters = {kNvidia, second};
  const std::vector<Gpu> gpus = ListGpus(adapters, {}, false);
  Check(gpus.size() == 2 && gpus[0].id != gpus[1].id, "the same model twice: two ids");
  Check(gpus[1].id == "pci:10de:2520:09261028:a1:1", "the second one has ordinal 1");
  const std::vector<std::string> ogre = {"(default)", kNvidia.description,
                                         kNvidia.description + " (2)"};
  Check(RenderingDevice(adapters, gpus[1].id, ogre) == kNvidia.description + " (2)",
        "the second one is OGRE's \"(2)\"");
  Check(RenderingDevice(adapters, gpus[0].id, ogre) == kNvidia.description, "the first one");
  // Lists that do not line up: the name alone cannot tell twins apart.
  Check(RenderingDevice(adapters, gpus[1].id, {"(default)", kNvidia.description}).empty(),
        "twins without OGRE's positions: automatic");
}

void TestListsOutOfStep() {
  // OGRE hides an adapter (NVIDIA PerfHUD): positions do not line up, the name still does.
  const Adapter perfhud{"NVIDIA PerfHUD", 0x10de, 0x0001, 0, 0, false, 44};
  const std::vector<Adapter> adapters = {perfhud, kIntel, kNvidia};
  const std::vector<std::string> ogre = {"(default)", kIntel.description, kNvidia.description};
  const std::string id = ListGpus(adapters, {}, false)[2].id;
  Check(RenderingDevice(adapters, id, ogre) == kNvidia.description, "found by its name");
}

void TestSoftwareOnly() {
  // No GPU driver: only the basic render driver. The menu's list is empty (automatic).
  const std::vector<Adapter> adapters = {kWarp};
  Check(ListGpus(adapters, {33}, false).empty(), "software only: nothing to offer");
  const std::vector<Gpu> all = ListGpus(adapters, {33}, true);
  Check(all.size() == 1 && all[0].software, "listed for testing (replay --list_gpus)");
  Check(RenderingDevice(adapters, all[0].id,
                        {"(default)", "Microsoft Basic Render Driver (software)"}) ==
            "Microsoft Basic Render Driver (software)",
        "WARP can be asked for by its id (replay --gpu)");
}

void TestNameMatching() {
  Adapter padded = kIntel;
  padded.description = "  " + kIntel.description + "  ";
  Check(RenderingDevice({padded}, ListGpus({padded}, {}, false)[0].id,
                        {"(default)", kIntel.description}) == kIntel.description,
        "OGRE trims the description");
  Adapter prefix = kIntel;
  prefix.description = "Intel(R) UHD Graphics";
  Check(RenderingDevice({prefix}, ListGpus({prefix}, {}, false)[0].id,
                        {"(default)", "Intel(R) UHD Graphics 630"}).empty(),
        "a longer name is not this adapter's");
}

}  // namespace

int main() {
  TestHybridLaptop();
  TestTwinGpus();
  TestListsOutOfStep();
  TestSoftwareOnly();
  TestNameMatching();
  std::printf("gpu_adapters test: ok\n");
  return 0;
}
