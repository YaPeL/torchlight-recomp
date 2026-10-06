#include "platform/gpu_adapters.h"

#include <cstdio>

namespace torchlight::platform::gpu_adapters {

namespace {

std::string Id(const std::vector<Adapter>& adapters, size_t index) {
  const Adapter& a = adapters[index];
  uint32_t ordinal = 0;
  for (size_t i = 0; i < index; ++i) {
    const Adapter& b = adapters[i];
    if (b.vendor == a.vendor && b.device == a.device && b.subsystem == a.subsystem &&
        b.revision == a.revision) {
      ++ordinal;
    }
  }
  char id[64];
  std::snprintf(id, sizeof(id), "pci:%04x:%04x:%08x:%02x:%u", a.vendor, a.device, a.subsystem,
                a.revision, ordinal);
  return id;
}

// OGRE's name for an adapter is its description, trimmed, maybe followed by " (N)" or
// " (software)" (D3D11Driver::DriverDescription).
bool NamesAdapter(const std::string& ogre_value, const std::string& description) {
  size_t begin = description.find_first_not_of(' ');
  size_t end = description.find_last_not_of(' ');
  if (begin == std::string::npos) return false;
  const std::string name = description.substr(begin, end - begin + 1);
  if (ogre_value.compare(0, name.size(), name) != 0) return false;
  return ogre_value.size() == name.size() || ogre_value.compare(name.size(), 2, " (") == 0;
}

}  // namespace

std::vector<Gpu> ListGpus(const std::vector<Adapter>& adapters,
                          const std::vector<uint64_t>& preference,
                          bool include_software) {
  std::vector<size_t> order;
  for (uint64_t luid : preference) {
    for (size_t i = 0; i < adapters.size(); ++i) {
      if (adapters[i].luid == luid) order.push_back(i);
    }
  }
  // Adapters the preference order missed (or no preference order): EnumAdapters1 order.
  for (size_t i = 0; i < adapters.size(); ++i) {
    bool listed = false;
    for (size_t j : order) listed = listed || j == i;
    if (!listed) order.push_back(i);
  }
  std::vector<Gpu> gpus;
  for (size_t i : order) {
    if (adapters[i].software && !include_software) continue;
    gpus.push_back({Id(adapters, i), adapters[i].description, adapters[i].software});
  }
  return gpus;
}

std::string RenderingDevice(const std::vector<Adapter>& adapters, const std::string& id,
                            const std::vector<std::string>& ogre_devices) {
  size_t index = adapters.size();
  for (size_t i = 0; i < adapters.size(); ++i) {
    if (Id(adapters, i) == id) index = i;
  }
  if (index == adapters.size()) return "";
  const std::string& description = adapters[index].description;
  // By position: OGRE lists "(default)", then the adapters in EnumAdapters1 order.
  if (ogre_devices.size() == adapters.size() + 1 &&
      NamesAdapter(ogre_devices[index + 1], description)) {
    return ogre_devices[index + 1];
  }
  // Otherwise only a value no other adapter's name could also produce.
  std::string found;
  for (const std::string& value : ogre_devices) {
    if (!NamesAdapter(value, description)) continue;
    if (!found.empty()) return "";
    found = value;
  }
  for (size_t i = 0; i < adapters.size(); ++i) {
    if (i != index && !found.empty() && NamesAdapter(found, adapters[i].description)) return "";
  }
  return found;
}

}  // namespace torchlight::platform::gpu_adapters
