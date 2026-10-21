// The quality levels (quality.h): names and the game settings each one writes.

#include <cstdio>

#include "hooks/quality.h"

using namespace torchlight::hooks;

int main() {
  int failures = 0;
  auto check = [&](bool ok, const char* what) {
    if (!ok) {
      std::fprintf(stderr, "FAIL: %s\n", what);
      ++failures;
    }
  };
  for (Quality q : {Quality::kNormal, Quality::kLow, Quality::kVeryLow}) {
    check(ParseQuality(QualityName(q)) == q, "each name parses back to its level");
  }
  check(!ParseQuality("ultra") && !ParseQuality(""), "unknown names are refused");
  check(!QualityShadowResolution(Quality::kNormal), "normal keeps the game's shadow resolution");
  check(QualityShadowResolution(Quality::kLow) == 256u, "low: 256, the PC's detail level 2");
  check(QualityShadowResolution(Quality::kVeryLow) == 128u, "very low: 128, its lowest");
  std::printf("quality_test: %s\n", failures ? "FAILED" : "OK");
  return failures ? 1 : 0;
}
