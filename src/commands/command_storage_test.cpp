// The gamma ramp command's host storage (types.h, TORCHLIGHT_COMPACT_COMMANDS): copies own their
// values and the wire body is the same in both layouts. Run with a capture to check that a real
// capture reads and writes back byte for byte:
//   command_storage_test [CAPTURE.tlcap]

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "commands/serialize.h"

namespace {

using namespace torchlight::commands;

int failures = 0;

void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

SetGammaRamp Ramp() {
  SetGammaRamp gamma;
  gamma.pwl = true;
  for (size_t i = 0; i < kGammaRampEntries; ++i) gamma.values[i] = uint16_t(i * 73 + 0x1234);
  return gamma;
}

void TestOwnership() {
  const SetGammaRamp gamma = Ramp();
  Check(gamma.values.size() == kGammaRampEntries, "the table has its fixed size");
  SetGammaRamp copy = gamma;
  copy.values[0] ^= 1;
  Check(gamma.values[0] == 0x1234 && !(copy == gamma), "a copy owns its values");
  // Growing a frame's command vector moves the commands: the values go with them.
  std::vector<Command> commands;
  for (uint16_t i = 0; i < 128; ++i) {
    copy.values[0] = i;
    commands.push_back({0, copy});
    commands.push_back({0, SetDepthCheck{bool(i & 1)}});
  }
  bool kept = true;
  for (uint16_t i = 0; i < 128; ++i) {
    const auto& g = std::get<SetGammaRamp>(commands[2 * i].payload);
    kept = kept && g.values.size() == kGammaRampEntries && g.values[0] == i &&
           g.values[767] == gamma.values[767];
  }
  Check(kept, "commands moved by the vector keep every value");
#ifdef TORCHLIGHT_COMPACT_COMMANDS
  Check(sizeof(Command) < 256, "the gamma table no longer sets the command's size");
#endif
}

void TestWire() {
  Capture c;
  c.meta.frame_count = 1;
  c.meta.first_swap = 17;
  c.frames.push_back({0, 17, {}});
  const SetGammaRamp gamma = Ramp();
  c.frames[0].commands.push_back({kFromBaseline, gamma});
  c.frames[0].commands.push_back({0, Present{17}});
  const std::vector<uint8_t> bytes = SerializeCapture(c);
  // Opcode 42, flags 1, length 1 + 768 * 2, pwl, then the values: no count, in either layout.
  std::vector<uint8_t> body{42, 0, 1, 0, 1, 6, 0, 0, 1};
  for (uint16_t v : gamma.values) {
    body.push_back(uint8_t(v));
    body.push_back(uint8_t(v >> 8));
  }
  Check(std::search(bytes.begin(), bytes.end(), body.begin(), body.end()) != bytes.end(),
        "the gamma ramp's wire body");
  Capture read;
  std::string error;
  Check(DeserializeCapture(bytes, read, error), "the capture reads");
  Check(SerializeCapture(read) == bytes, "and writes back the same bytes");
}

void TestCaptureFile(const char* path) {
  Capture capture;
  std::string error;
  std::vector<uint8_t> bytes;
  if (FILE* f = std::fopen(path, "rb")) {
    std::fseek(f, 0, SEEK_END);
    bytes.resize(size_t(std::ftell(f)));
    std::fseek(f, 0, SEEK_SET);
    Check(std::fread(bytes.data(), 1, bytes.size(), f) == bytes.size(), "capture file read");
    std::fclose(f);
  }
  Check(DeserializeCapture(bytes, capture, error), error.c_str());
  Check(SerializeCapture(capture) == bytes, "the capture writes back byte for byte");
  size_t commands = 0;
  for (const Frame& frame : capture.frames) commands += frame.commands.size();
  std::printf("%s: %zu frames, %zu commands, %zu bytes of host commands\n", path,
              capture.frames.size(), commands, commands * sizeof(Command));
}

}  // namespace

int main(int argc, char** argv) {
  TestOwnership();
  TestWire();
  for (int i = 1; i < argc; ++i) TestCaptureFile(argv[i]);
  std::printf("command_storage_test: Command %zu bytes, SetGammaRamp %zu; %s\n", sizeof(Command),
              sizeof(SetGammaRamp), failures ? "FAILED" : "OK");
  return failures ? 1 : 0;
}
