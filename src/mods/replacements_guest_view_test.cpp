// Tests for what the guest is not shown of the replacements folder (replacements_guest_view.h).

#include "mods/replacements_guest_view.h"

#include <cstdio>
#include <cstdlib>

namespace m = torchlight::mods;

namespace {

int failures = 0;

void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

}  // namespace

int main() {
  Check(m::GuestViewKey("media\\LevelSets\\town1/Bricks.dds") == "MEDIA/LEVELSETS/TOWN1/BRICKS.DDS",
        "keys: upper case, '/' separators");
  Check(m::GuestViewKey("/media/x.dds") == "MEDIA/X.DDS", "keys: no leading separator");
  Check(m::IsImageFile("a/b.DDS") && m::IsImageFile("c.png") && m::IsImageFile("d.Tga") &&
            m::IsImageFile("e.jpg") && m::IsImageFile("f.jpeg") && m::IsImageFile("g.bmp"),
        "image extensions, any case");
  Check(!m::IsImageFile("a/b.mesh") && !m::IsImageFile("a/b.material") && !m::IsImageFile("noext") &&
            !m::IsImageFile("dir.dds/file"),
        "not images");

  const std::unordered_set<std::string> pak = {"MEDIA/LEVELSETS/TOWN1/BRICKS.DDS", "MEDIA/UI/WIDGETS1.DDS",
                                               "MEDIA/MODELS/BARREL/BARREL.MESH"};
  const auto pak_has = [&](const std::string& key) { return pak.contains(key); };
  const std::unordered_set<std::string> imagesets = {"MEDIA/UI/WIDGETS1.DDS"};
  const auto hidden = m::GuestHiddenReplacements(
      {"media/levelsets/town1/Bricks.DDS", "media\\ui\\widgets1.dds", "media/levelsets/town1/new_moss.dds",
       "media/models/barrel/barrel.mesh"},
      pak_has, imagesets);
  Check(hidden.size() == 1 && hidden.contains("MEDIA/LEVELSETS/TOWN1/BRICKS.DDS"),
        "only the pak's image no imageset names is hidden, matched in any case and separator");
  Check(!hidden.contains("MEDIA/UI/WIDGETS1.DDS"), "an imageset's texture stays visible");
  Check(!hidden.contains("MEDIA/LEVELSETS/TOWN1/NEW_MOSS.DDS"), "an image the pak lacks stays visible");
  Check(!hidden.contains("MEDIA/MODELS/BARREL/BARREL.MESH"), "a mesh stays visible");
  Check(m::GuestHiddenReplacements({}, pak_has, imagesets).empty(), "no files: nothing hidden");
  const std::unordered_set<std::string> bare = {"BRICKS.DDS"};
  Check(m::GuestHiddenReplacements({"media/levelsets/town1/bricks.dds"}, pak_has, bare).empty(),
        "an imageset naming the file by its bare name keeps it visible");

  if (failures) return EXIT_FAILURE;
  std::printf("replacements_guest_view_test: ok\n");
  return EXIT_SUCCESS;
}
