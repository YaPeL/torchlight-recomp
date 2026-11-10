// A pack's Serif14 made from its Serif (pack_fonts.h): the font element's name and size changed and
// nothing else; written only for a pack with Serif.font and no Serif14.font, in any case.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "game_menu/pack_fonts.h"

namespace {

namespace fs = std::filesystem;
using torchlight::game_menu::DerivedFont;
using torchlight::game_menu::WriteMissingSerif14;

void Check(bool ok, const std::string& what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    std::exit(1);
  }
}

// As GOG's Russian Serif.font (a PC font with its script): the game's attributes, another file.
constexpr const char* kSerif =
    "<?xml version=\"1.0\" ?>\r\n<Font Name=\"Serif\" Filename=\"media/ui/ariblk.ttf\" Type=\"FreeType\" "
    "Size=\"11\" NativeHorzRes=\"1024\" NativeVertRes=\"768\" AutoScaled=\"true\" AntiAlias=\"true\" />\r\n";

void Write(const fs::path& path, const std::string& text) {
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << text;
}

std::string Read(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

}  // namespace

int main() {
  const auto serif14 = DerivedFont(kSerif, "Serif14", 14);
  Check(serif14 &&
            *serif14 ==
                "<?xml version=\"1.0\" ?>\r\n<Font Name=\"Serif14\" Filename=\"media/ui/ariblk.ttf\" "
                "Type=\"FreeType\" Size=\"14\" NativeHorzRes=\"1024\" NativeVertRes=\"768\" AutoScaled=\"true\" "
                "AntiAlias=\"true\" />\r\n",
        "the name and the size, nothing else");
  Check(!DerivedFont("<Font Name=\"Serif\" />", "Serif14", 14), "no size: nothing");
  Check(!DerivedFont("not a font", "Serif14", 14), "no <Font: nothing");
  Check(!DerivedFont("<Font Size=\"11\" />", "Serif14", 14), "no name: nothing");

  const fs::path root = fs::temp_directory_path() / "pack_fonts_test";
  fs::remove_all(root);
  std::string error;
  // A pack as GOG's (other case in the folder names), no Serif14: written.
  Write(root / "ru/Media/ui/SERIF.font", kSerif);
  Check(WriteMissingSerif14(root / "ru", root / "out", error) && error.empty(), "GOG-like pack: written");
  Check(Read(root / "out/media/UI/Serif14.font") == *serif14, "the derived font written");
  // The pack has its own Serif14: kept, nothing written.
  fs::remove_all(root / "out");
  Write(root / "ru/Media/ui/serif14.FONT", "its own");
  Check(!WriteMissingSerif14(root / "ru", root / "out", error) && error.empty() && !fs::exists(root / "out"),
        "the pack's own Serif14: nothing written");
  // No fonts in the pack (the translation only): nothing.
  Write(root / "de/translation.dat.adm", "x");
  Check(!WriteMissingSerif14(root / "de", root / "out", error) && error.empty(), "no fonts: nothing");
  // A Serif.font that is not a font definition: said so.
  Write(root / "xx/media/UI/Serif.font", "garbage");
  Check(!WriteMissingSerif14(root / "xx", root / "out", error) && !error.empty(), "a broken Serif.font: an error");
  fs::remove_all(root);

  std::puts("pack_fonts_test: ok");
  return 0;
}
