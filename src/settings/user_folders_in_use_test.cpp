// Tests for the folders in use (user_folders_in_use.h): the platform's without roots given, and
// with --user_data_root or --cache_root nothing of the player's folders left in use.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "settings/user_folders_in_use.h"

namespace {

using namespace torchlight::settings;
namespace fs = std::filesystem;

void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    std::exit(1);
  }
}

bool Below(const fs::path& path, const fs::path& root) {
  const std::string p = path.lexically_normal().generic_string();
  const std::string r = root.lexically_normal().generic_string();
  return !path.empty() && p.compare(0, r.size(), r) == 0;
}

UserRoots Player() {
  UserRoots r;
  r.platform_data = "/home/p/.local/share/TorchlightRecomp";
  r.platform_cache = "/home/p/.cache/TorchlightRecomp";
  // The runtime's defaults (live/install.cpp ConfigurePaths).
  r.user_data_root = r.platform_data;
  r.cache_root = r.platform_data / "cache";
  return r;
}

void TestDefaults() {
  const UserRoots r = Player();
  const FoldersInUse f = ResolveFoldersInUse(r);
  Check(f.data == r.platform_data, "defaults: the platform's data folder");
  Check(f.cache == r.platform_data / "cache", "defaults: unitdata in <data>/cache as before");
  Check(f.generated == r.platform_cache, "defaults: atlases and prompts in the platform's cache");
  Check(f.achievements == r.platform_data / "achievements", "defaults: achievements in <data>");
}

void TestNoPlatformFolder() {
  const FoldersInUse f = ResolveFoldersInUse(UserRoots{});
  Check(f.data.empty() && f.cache.empty() && f.generated.empty() && f.achievements.empty(),
        "no platform folder, no roots: nothing");
}

void TestUserDataRoot() {
  UserRoots r = Player();
  r.user_data_root = "/tmp/run/data";
  r.cache_root = "/tmp/run/data/cache";  // the SDK's default below the root given
  r.user_data_root_given = true;
  const FoldersInUse f = ResolveFoldersInUse(r);
  for (const fs::path& p : {f.data, f.cache, f.generated, f.achievements}) {
    Check(Below(p, "/tmp/run/data"), "--user_data_root: every folder below it");
    Check(!Below(p, r.platform_data) && !Below(p, r.platform_cache), "--user_data_root: none of the player's");
  }
  Check(f.achievements == fs::path("/tmp/run/data/achievements"), "--user_data_root: achievements");
  Check(f.cache == fs::path("/tmp/run/data/cache"), "--user_data_root: the runtime's cache");
}

void TestCacheRootOnly() {
  UserRoots r = Player();
  r.cache_root = "/tmp/run/cache";
  r.cache_root_given = true;
  const FoldersInUse f = ResolveFoldersInUse(r);
  Check(f.data == r.platform_data, "--cache_root alone: the data folder stays");
  Check(f.cache == fs::path("/tmp/run/cache") && f.generated == fs::path("/tmp/run/cache"),
        "--cache_root alone: every cache below it");
}

void TestBothRoots() {
  UserRoots r = Player();
  r.user_data_root = "/tmp/run/data";
  r.cache_root = "/tmp/run/cache";
  r.user_data_root_given = r.cache_root_given = true;
  const FoldersInUse f = ResolveFoldersInUse(r);
  Check(f.data == fs::path("/tmp/run/data") && f.achievements == fs::path("/tmp/run/data/achievements"),
        "both roots: data and achievements below the data root");
  Check(f.cache == fs::path("/tmp/run/cache") && f.generated == fs::path("/tmp/run/cache"),
        "both roots: caches below the cache root");
}

}  // namespace

int main() {
  TestDefaults();
  TestNoPlatformFolder();
  TestUserDataRoot();
  TestCacheRootOnly();
  TestBothRoots();
  std::puts("user_folders_in_use_test: ok");
  return 0;
}
