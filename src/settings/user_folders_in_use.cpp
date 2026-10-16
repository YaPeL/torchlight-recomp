#include "settings/user_folders_in_use.h"

namespace torchlight::settings {

FoldersInUse ResolveFoldersInUse(const UserRoots& roots) {
  FoldersInUse f;
  const bool own = roots.user_data_root_given || roots.cache_root_given;
  f.data = roots.user_data_root_given ? roots.user_data_root : roots.platform_data;
  if (own) {
    f.cache = roots.cache_root;
    f.generated = roots.cache_root;
  } else {
    if (!roots.platform_data.empty()) f.cache = roots.platform_data / "cache";
    f.generated = roots.platform_cache;
  }
  if (!f.data.empty()) f.achievements = f.data / "achievements";
  return f;
}

}  // namespace torchlight::settings
