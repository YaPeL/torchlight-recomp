// Tests for the named texture refresh (named_texture_refresh.h): the keys names are compared by,
// the queue the backend thread takes, and the live content source's textures that match.

#include <cstdio>
#include <cstdlib>

#include "live/live_content_source.h"
#include "live/named_texture_refresh.h"

namespace {

using namespace torchlight::commands;
using torchlight::live::LiveContentSource;
using torchlight::live::LiveFrame;
using torchlight::live::RefreshKey;
using torchlight::live::RefreshNamedTextures;
using torchlight::live::TakeTextureRefreshes;

int failures = 0;

void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

TextureDesc Named(uint32_t address, const char* name) {
  TextureDesc t;
  t.id = {ResourceKind::kTexture, address, 1};
  t.name = name;
  return t;
}

}  // namespace

int main() {
  // Keys: case, separators, a leading '/', our devices.
  Check(RefreshKey("media/ui/icons2/widgets1.dds") == "MEDIA/UI/ICONS2/WIDGETS1.DDS", "a pak name");
  Check(RefreshKey("Media\\UI\\Icons2\\Widgets1.DDS") == "MEDIA/UI/ICONS2/WIDGETS1.DDS", "any case and separator");
  Check(RefreshKey("tlprompt:/media/ui/icons2/widgets1.dds") == "MEDIA/UI/ICONS2/WIDGETS1.DDS",
        "the prompts' device dropped");
  Check(RefreshKey("TLMOD002:\\MEDIA\\X.DDS") == "MEDIA/X.DDS", "a mod's device dropped");
  Check(RefreshKey("/media/x.dds") == "MEDIA/X.DDS", "a leading separator dropped");

  // The queue: names gathered without duplicates, taken once.
  Check(TakeTextureRefreshes().empty(), "nothing asked: empty");
  RefreshNamedTextures({"media/ui/icons2/widgets1.dds", "MEDIA/UI/ICONS2/WIDGETS1.DDS"});
  RefreshNamedTextures({"tlprompt:/media/ui/icons2/widgets.dds", ""});
  const auto keys = TakeTextureRefreshes();
  Check(keys.size() == 2, "two distinct names, the empty one ignored");
  Check(TakeTextureRefreshes().empty(), "taken: empty again");

  // The content source's textures that match: named ones only, by key, any spelling.
  torchlight::frontend::ZipArchive pak;  // not opened: no game files needed here
  LiveContentSource source(pak);
  LiveFrame f;
  f.textures.push_back(Named(0x1000, "media/ui/icons2/widgets1.dds"));
  f.textures.push_back(Named(0x2000, "TLPROMPT:/MEDIA/UI/ICONS2/WIDGETS.DDS"));
  f.textures.push_back(Named(0x3000, "media/ui/icons2/other.dds"));
  TextureDesc manual = Named(0x4000, "media/ui/icons2/widgets1.dds");
  manual.manual = true;
  f.textures.push_back(manual);
  TextureDesc target = Named(0x5000, "media/ui/icons2/widgets1.dds");
  target.render_target = true;
  f.textures.push_back(target);
  source.Apply(f);
  source.Finish();
  const auto found = source.NamedTextures(keys);
  bool widgets1 = false, widgets = false;
  for (const auto& t : found) {
    widgets1 |= t.id.guest_address == 0x1000;
    widgets |= t.id.guest_address == 0x2000;
  }
  Check(found.size() == 2 && widgets1 && widgets,
        "the two named widgets textures; not the other one, the manual one or the render target");
  Check(source.NamedTextures({}).empty(), "no keys: none");

  if (failures) return EXIT_FAILURE;
  std::printf("named_texture_refresh_test: ok\n");
  return EXIT_SUCCESS;
}
