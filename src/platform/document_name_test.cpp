// The name a picked document's copy gets (document_name.h), from content URIs as Android's
// providers write them.

#include <cstdio>
#include <string>

#include "platform/document_name.h"

namespace {
int failures = 0;
void Check(const std::string& uri, const std::string& expected) {
  const std::string got = torchlight::platform::PickedDocumentName(uri);
  if (got != expected) {
    ++failures;
    std::fprintf(stderr, "FAIL: %s -> %s (expected %s)\n", uri.c_str(), got.c_str(),
                 expected.c_str());
  }
}
}  // namespace

int main() {
  const std::string external = "content://com.android.externalstorage.documents/document/";
  const std::string downloads = "content://com.android.providers.downloads.documents/document/";
  Check(external + "primary%3ADownload%2FMyMod.zip", "MyMod.zip");
  Check(external + "primary%3AMy%20Mod%20v2.zip", "My Mod v2.zip");
  Check(downloads + "raw%3A%2Fstorage%2Femulated%2F0%2FDownload%2FPack.zip", "Pack.zip");
  Check(downloads + "msf%3A1234", "picked_file");
  Check(downloads + "1234", "picked_file");
  Check(external + "primary%3ADownload%2F..", "picked_file");
  Check(external + "primary%3A.hidden", "picked_file");
  Check(external + "primary%3Aa%5C..%5Cb.zip", "picked_file");
  Check(external + "primary%3AMod.zip?x=1", "Mod.zip");
  Check(external + "primary%3A%E2%9C%93.zip", "\xE2\x9C\x93.zip");
  Check("", "picked_file");
  if (failures) return 1;
  std::puts("document_name_test: ok");
  return 0;
}
