// Tests for the save import's third-party pieces: SHA-256 (FIPS 180-2 vectors) and raw inflate
// (a stream made by zlib with wbits -15, like the game paks' deflate entries).

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "save_import/digest.h"
#include "save_import/inflate.h"

namespace {

using namespace torchlight::save_import;

void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    std::exit(1);
  }
}

std::string Hex(const Sha256Digest& digest) {
  std::string out;
  for (uint8_t byte : digest) {
    char pair[3];
    std::snprintf(pair, sizeof(pair), "%02x", byte);
    out += pair;
  }
  return out;
}

Sha256Digest Of(std::string_view text) {
  return Sha256({reinterpret_cast<const uint8_t*>(text.data()), text.size()});
}

void TestSha256() {
  Check(Hex(Of("")) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "empty");
  Check(Hex(Of("abc")) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "abc");
  Check(Hex(Of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")) ==
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
        "two blocks");
  Check(Hex(Of(std::string(1000000, 'a'))) ==
            "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
        "a million a");
}

// 40 x "KEY :value\n" + "the end", deflated by zlib (level 9, raw).
const std::vector<uint8_t> kDeflated = {0xf3, 0x76, 0x8d, 0x54, 0xb0, 0x2a, 0x4b, 0xcc, 0x29,
                                        0x4d, 0xe5, 0xf2, 0x1e, 0x65, 0x0e, 0x1d, 0x66, 0x49,
                                        0x46, 0xaa, 0x42, 0x6a, 0x5e, 0x0a, 0x00};

std::string Expected() {
  std::string text;
  for (int i = 0; i < 40; ++i) text += "KEY :value\n";
  return text + "the end";
}

void TestInflate() {
  const std::string expected = Expected();
  std::vector<uint8_t> out;
  Check(Inflate(kDeflated, expected.size(), out), "inflates");
  Check(std::string(out.begin(), out.end()) == expected, "inflated text");
  Check(Hex(Sha256(out)) == "08da72da484a62c3c2b083dfafe51766cc7d6d87e85a366cf6a93cc594a7590c",
        "inflated digest");

  Check(!Inflate(kDeflated, expected.size() - 1, out) && out.empty(), "shorter size refused");
  Check(!Inflate(kDeflated, expected.size() + 1, out) && out.empty(), "longer size refused");
  std::vector<uint8_t> damaged(kDeflated.begin(), kDeflated.end() - 6);
  Check(!Inflate(damaged, expected.size(), out) && out.empty(), "truncated stream refused");
}

}  // namespace

int main() {
  TestSha256();
  TestInflate();
  std::puts("save_import deps: ok");
  return 0;
}
