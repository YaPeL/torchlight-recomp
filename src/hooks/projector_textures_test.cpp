// The projector textures' choice by listener, its control by data and the every-other-frame
// alternation (projector_textures.h).

#include <cstdio>

#include "hooks/projector_textures.h"

using namespace torchlight::hooks;

namespace {

int failures = 0;

void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

constexpr ProjectorTextureFacts kShadow{512, 512, 1, true, 0x42F47A30};
constexpr ProjectorTextureFacts kLight{512, 512, 1, true, 0x42F47A30};

}  // namespace

int main() {
  // The choice is the listener's, whatever the data.
  Check(IsProjectorTexture(kShadow), "the game client's texture is chosen");
  Check(!IsProjectorTexture({512, 512, 1, false, 0x1000}), "another listener's is not");
  Check(IsProjectorTexture({256, 256, 2, true, 0x1000}), "chosen even when the data differs");

  // Every other update per target, each target on its own count, starting with an update.
  EveryOtherFrame alternation;
  bool runs[6];
  for (int i = 0; i < 3; ++i) {
    runs[2 * i] = alternation.Run(1);
    runs[2 * i + 1] = alternation.Run(2);
  }
  Check(runs[0] && runs[1], "first updates run");
  Check(!runs[2] && !runs[3], "second ones skip");
  Check(runs[4] && runs[5], "third ones run");
  alternation.Forget(1);
  Check(alternation.Run(1), "a forgotten target starts again with an update");

  // The control: nothing when listener and data agree; each disagreement once.
  ProjectorControl control;
  Check(!control.Check(1, kShadow) && !control.Check(2, kLight), "the two found agree");
  Check(!control.Check(3, {1024, 1024, 0, false, 0}), "an unrelated target says nothing");
  Check(control.Check(4, {256, 256, 1, true, 0x42F47A30}).has_value(),
        "chosen without the data is noted");
  Check(!control.Check(5, {128, 128, 1, true, 0x42F47A30}), "only once");
  Check(control.Check(6, {512, 512, 1, false, 0x5000}).has_value(),
        "the data without the listener is noted");
  Check(!control.Check(7, {512, 512, 1, false, 0x6000}), "only once");
  Check(control.Check(8, {512, 512, 1, true, 0x7000}).has_value(),
        "a chosen target with another listener than the first is noted");

  std::printf("projector_textures_test: %s\n", failures ? "FAILED" : "OK");
  return failures ? 1 : 0;
}
