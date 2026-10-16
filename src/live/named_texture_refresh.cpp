#include "live/named_texture_refresh.h"

#include <algorithm>
#include <mutex>
#include <utility>

#include "mods/texture_files.h"

namespace torchlight::live {

namespace {

std::mutex g_mutex;
std::vector<std::string> g_pending;  // RefreshKey'd, no duplicates

}  // namespace

std::string RefreshKey(std::string_view name) {
  std::string key = mods::TextureNameKey(name);
  // "TLPROMPT:/MEDIA/..." -> "MEDIA/...": a device is a name ending in ':' before the first '/'.
  const size_t colon = key.find(':');
  if (colon != std::string::npos && key.find('/') > colon) {
    key.erase(0, colon + 1);
    while (!key.empty() && key.front() == '/') key.erase(key.begin());
  }
  return key;
}

void RefreshNamedTextures(std::vector<std::string> names) {
  std::lock_guard lock(g_mutex);
  for (const std::string& name : names) {
    std::string key = RefreshKey(name);
    if (!key.empty() && std::find(g_pending.begin(), g_pending.end(), key) == g_pending.end()) {
      g_pending.push_back(std::move(key));
    }
  }
}

std::vector<std::string> TakeTextureRefreshes() {
  std::lock_guard lock(g_mutex);
  return std::exchange(g_pending, {});
}

}  // namespace torchlight::live
