// Tests for following the guest's texture opens (texture_open.h).

#include <cstdio>
#include <cstdlib>

#include "game_menu/texture_open.h"

namespace {

using torchlight::game_menu::IsTextureName;
using torchlight::game_menu::TextureOpenRecorder;

void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    std::exit(1);
  }
}

void TestNames() {
  Check(IsTextureName("MEDIA/LEVELSETS/MINE/MINE_FLOOR01.DDS"), "upper-case .DDS");
  Check(IsTextureName("media/x.png") && IsTextureName("a.Tga") && IsTextureName("b.jpeg"), "other kinds");
  Check(!IsTextureName("MEDIA/MODELS/WEAPONS/HACKER/HACKER.MESH"), "a mesh");
  Check(!IsTextureName("x.material") && !IsTextureName(".DDS") && !IsTextureName(""), "not textures");
}

void TestRecorder() {
  TextureOpenRecorder r;
  // A texture found in the first group asked.
  r.Begin("MEDIA/X.DDS", 0x1000);
  r.ArchiveOpened("game:\\pak.zip", "media/x.dds");
  auto o = r.End();
  Check(o && o->name == "MEDIA/X.DDS" && o->container == "game:\\pak.zip" && o->path == "media/x.dds",
        "one open");
  // Not in its group: openResource calls itself for the group that has it; the inner open counts.
  r.Begin("MEDIA/Y.DDS", 0x1000);
  r.Begin("MEDIA/Y.DDS", 0xF00);
  r.ArchiveOpened("tlmod001:\\", "MEDIA/Y.DDS");
  Check(!r.End(), "an inner call reports nothing");
  o = r.End();
  Check(o && o->container == "tlmod001:\\", "the outermost call reports the inner open");
  // The last archive opened wins.
  r.Begin("MEDIA/Z.DDS", 0x1000);
  r.ArchiveOpened("game:\\pak.zip", "media/z.dds");
  r.ArchiveOpened("tlrepl:\\", "MEDIA/Z.DDS");
  o = r.End();
  Check(o && o->container == "tlrepl:\\", "the last open");
  // Not a texture, or nothing opened (not found: the game throws, End still runs).
  r.Begin("MEDIA/A.MESH", 0x1000);
  r.ArchiveOpened("tlmod000:\\", "MEDIA/A.MESH");
  Check(!r.End(), "a mesh is not reported");
  r.Begin("MEDIA/B.DDS", 0x1000);
  Check(!r.End(), "nothing opened");
  // Opens outside any openResource call are ignored, and End without Begin does nothing.
  r.ArchiveOpened("tlmod000:\\", "MEDIA/C.DDS");
  Check(!r.End(), "no call");
  r.Begin("MEDIA/D.DDS", 0x1000);
  Check(!r.End(), "an open from before is not carried over");
  // A call the game threw past (no End): the next outer call starts over.
  r.Begin("MEDIA/LOST.DDS", 0x1000);
  r.Begin("MEDIA/LOST.DDS", 0xF00);
  r.Begin("MEDIA/E.DDS", 0x1000);
  r.ArchiveOpened("tlrepl:\\", "MEDIA/E.DDS");
  o = r.End();
  Check(o && o->name == "MEDIA/E.DDS" && o->container == "tlrepl:\\", "a lost call does not hide the next");
}

}  // namespace

int main() {
  TestNames();
  TestRecorder();
  std::puts("texture_open_test: ok");
  return 0;
}
