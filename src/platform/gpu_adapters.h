// The GPU choice on Windows from DXGI's adapters, as pure functions over a description of them (no
// Windows headers), so the rules are tested with made-up machines on any platform
// (gpu_adapters_test). platform_win.cpp feeds them DXGI's enumeration.

#ifndef TORCHLIGHT_PLATFORM_GPU_ADAPTERS_H_
#define TORCHLIGHT_PLATFORM_GPU_ADAPTERS_H_

#include <cstdint>
#include <string>
#include <vector>

namespace torchlight::platform::gpu_adapters {

// One DXGI adapter (DXGI_ADAPTER_DESC1).
struct Adapter {
  std::string description;  // UTF-8
  uint32_t vendor = 0, device = 0, subsystem = 0, revision = 0;
  bool software = false;    // DXGI_ADAPTER_FLAG_SOFTWARE (WARP, the basic render driver)
  uint64_t luid = 0;        // identifies the adapter within this boot only
};

// A GPU as the settings store it: "pci:VVVV:DDDD:SSSSSSSS:RR:N" (hexadecimal vendor, device,
// subsystem and revision ids, then the ordinal among adapters with those same four ids, in
// EnumAdapters1 order), stable across boots and driver updates, unlike the LUID or the name.
struct Gpu {
  std::string id;
  std::string name;
  bool software = false;
};

// `adapters` in EnumAdapters1 order (the order OGRE's Direct3D 11 render system lists them in);
// `preference` the LUIDs in the order to show them (EnumAdapterByGpuPreference with
// DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE; empty: EnumAdapters1 order). Software adapters are left
// out unless `include_software` (they are for testing, never offered to the player).
std::vector<Gpu> ListGpus(const std::vector<Adapter>& adapters,
                          const std::vector<uint64_t>& preference,
                          bool include_software);

// OGRE's "Rendering Device" value for the GPU `id`, among `ogre_devices` (the option's possible
// values, "(default)" first, then one per adapter in EnumAdapters1 order). The adapter is found by
// its id in `adapters` (EnumAdapters1 order) and crossed with OGRE's list by position, checking
// that OGRE's value is the adapter's own name (OGRE appends " (2)" or " (software)"); when the
// lists do not line up, a value naming that adapter alone is taken. Empty when there is no match:
// the caller keeps the automatic choice.
std::string RenderingDevice(const std::vector<Adapter>& adapters, const std::string& id,
                            const std::vector<std::string>& ogre_devices);

}  // namespace torchlight::platform::gpu_adapters

#endif
