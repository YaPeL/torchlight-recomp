// WrapProgramCache / UnwrapProgramCache: the program cache file's trailer.

#include <cstdio>
#include <string>

#include "backend/program_cache_file.h"

namespace {
int g_failures = 0;
void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++g_failures;
  }
}
}  // namespace

int main() {
  using torchlight::backend::UnwrapProgramCache;
  using torchlight::backend::WrapProgramCache;
  std::string payload(5000, '\0');
  for (size_t i = 0; i < payload.size(); ++i) payload[i] = char(i * 31 + 7);
  const std::string file = WrapProgramCache(payload);
  auto back = UnwrapProgramCache(file);
  Check(back && *back == payload, "a whole file gives OGRE's bytes back");
  Check(UnwrapProgramCache(WrapProgramCache("")).value_or("x").empty(),
        "an empty cache round-trips");

  Check(!UnwrapProgramCache(""), "an empty file is unreadable");
  Check(!UnwrapProgramCache(file.substr(0, file.size() / 2)), "a file cut in half is unreadable");
  Check(!UnwrapProgramCache(file.substr(0, file.size() - 1)), "a file one byte short is unreadable");
  Check(!UnwrapProgramCache(payload), "OGRE's bytes without the trailer are unreadable");
  std::string damaged = file;
  damaged[100] ^= 1;
  Check(!UnwrapProgramCache(damaged), "a damaged byte is noticed");
  return g_failures ? 1 : 0;
}
