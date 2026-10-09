// Tests for the first start's checks and install (game_files.h, install.h), with invented files and
// tables and a synthetic package header: no game data. `game_setup_test PACKAGE` instead extracts
// the user's own package and checks it against the real table.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "game_setup/game_files.h"
#include "game_setup/install.h"

namespace {

namespace fs = std::filesystem;
using namespace torchlight::game_setup;

void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    std::exit(1);
  }
}

void Write(const fs::path& file, const std::string& text) {
  fs::create_directories(file.parent_path());
  std::ofstream(file, std::ios::binary) << text;
}

// SHA-256 of "abc" and of "xyz".
constexpr const char* kAbc = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
constexpr const char* kXyz = "3608bca1e44ea6c4d268eb6db02260269892c0b42b86bbf1e77a6fa16c3c9282";

const GameFile kFiles[] = {
    {"default.xex", 3, kAbc},
    {"music/song.ogg", 3, kXyz},
};

struct Scratch {
  fs::path root = fs::temp_directory_path() / "tl_game_setup_test";
  Scratch() {
    fs::remove_all(root);
    fs::create_directories(root);
  }
  ~Scratch() { fs::remove_all(root); }
  fs::path Source(const std::string& xex = "abc", const std::string& song = "xyz") const {
    const fs::path dir = root / "source";
    fs::remove_all(dir);
    Write(dir / "default.xex", xex);
    Write(dir / "music" / "song.ogg", song);
    return dir;
  }
};

void TestCheckPackage() {
  PackageFacts facts{true, kContentTypeArcadeTitle, kTitleId};
  Check(CheckPackage(facts).empty(), "the game's package passes");
  Check(!CheckPackage({false, kContentTypeArcadeTitle, kTitleId}).empty(), "bad magic");
  Check(Render(CheckPackage({true, 0x00000001, kTitleId})).find("Arcade") != std::string::npos,
        "not an arcade title");
  Check(Render(CheckPackage({true, kContentTypeArcadeTitle, 0x4D5307E6})).find("not Torchlight") !=
            std::string::npos,
        "another game");
}

// A package header with the fields the checks read (offsets of the STFS header: magic 0x000,
// content type 0x344, the execution info's title ID 0x360), the rest zero.
void TestReadPackageFacts() {
  Scratch s;
  std::vector<char> header(0xA000, 0);
  std::memcpy(header.data(), "LIVE", 4);
  const unsigned char type[] = {0x00, 0x0D, 0x00, 0x00}, title[] = {0x58, 0x41, 0x0A, 0x7E};
  std::memcpy(header.data() + 0x344, type, 4);
  std::memcpy(header.data() + 0x360, title, 4);
  const fs::path package = s.root / "package";
  std::ofstream(package, std::ios::binary).write(header.data(), std::streamsize(header.size()));
  const auto facts = ReadPackageFacts(package);
  Check(facts && facts->magic_valid && facts->content_type == kContentTypeArcadeTitle &&
            facts->title_id == kTitleId && CheckPackage(*facts).empty(),
        "header fields read");
  std::memcpy(header.data(), "XXXX", 4);
  std::ofstream(package, std::ios::binary).write(header.data(), std::streamsize(header.size()));
  const auto bad = ReadPackageFacts(package);
  Check(!bad || !CheckPackage(*bad).empty(), "a file without a package magic is rejected");
  Write(s.root / "small", "LIVE");
  const auto small = ReadPackageFacts(s.root / "small");
  Check(!small || !CheckPackage(*small).empty(), "a short file is rejected");
}

void TestVerifyFolder() {
  Scratch s;
  Check(VerifyGameFolder(s.Source(), kFiles).empty(), "every file matches");
  Check(XexMatches(s.Source(), kFiles), "quick check of default.xex");
  fs::remove(s.Source() / "music" / "song.ogg");
  Check(Render(VerifyGameFolder(s.root / "source", kFiles)).find("music/song.ogg is missing") !=
            std::string::npos,
        "missing file");
  Check(Render(VerifyGameFolder(s.Source("abcd"), kFiles)).find("size") != std::string::npos,
        "another size");
  Check(!XexMatches(s.root / "source", kFiles), "quick check: another default.xex");
  Check(Render(VerifyGameFolder(s.Source("abd"), kFiles)).find("differs") != std::string::npos,
        "same size, other content");
  Check(Cancelled(VerifyGameFolder(s.Source(), kFiles, [](uint64_t, uint64_t) { return false; })),
        "cancelled");
}

// The check at every start: every file with its size, and default.xex's SHA-256.
void TestQuickCheck() {
  Scratch s;
  Check(QuickCheckGameFolder(s.Source(), kFiles).empty(), "quick check: a whole game");
  // A half install (the .xex there, the rest not): what the .xex alone would not see.
  fs::remove(s.root / "source" / "music" / "song.ogg");
  Check(XexMatches(s.root / "source", kFiles), "the .xex alone looks fine");
  Check(QuickCheckGameFolder(s.root / "source", kFiles).text == kTextMissing,
        "quick check: a missing file");
  Check(QuickCheckGameFolder(s.Source("abc", "xy"), kFiles).text == kTextOtherSize,
        "quick check: a file of another size");
  const Message xex = QuickCheckGameFolder(s.Source("abd"), kFiles);
  Check(xex.text == kTextDiffers && xex.values.at(0).second == "default.xex",
        "quick check: another default.xex of the same size");
  Check(QuickCheckGameFolder(s.root / "nowhere", kFiles).text == kTextMissing,
        "quick check: no folder");
}

void TestInstallFromFolder() {
  Scratch s;
  const fs::path game = s.root / "game";
  auto result = Install(Source::kFolder, s.Source(), game, kFiles, {});
  Check(result.error.empty() && result.moved_to.empty(), "installed");
  Check(VerifyGameFolder(game, kFiles).empty() && !fs::exists(s.root / "game.partial"),
        "complete, nothing partial left");
  Write(game / "import" / "mine.sav", "keep");
  result = Install(Source::kFolder, s.Source(), game, kFiles, {});
  Check(result.error.empty() && !result.moved_to.empty(), "reinstalled");
  Check(fs::exists(fs::path(result.moved_to) / "import" / "mine.sav"),
        "the previous install moved aside with its files, not deleted");
  // A bad source leaves the install as it was.
  result = Install(Source::kFolder, s.Source("abd"), game, kFiles, {});
  Check(!result.error.empty() && VerifyGameFolder(game, kFiles).empty() &&
            !fs::exists(s.root / "game.partial"),
        "bad source: install untouched, nothing partial left");
  result = Install(Source::kFolder, s.Source(), s.root / "other", kFiles,
                   [](Phase, uint64_t, uint64_t) { return false; });
  Check(Cancelled(result.error) && !fs::exists(s.root / "other") &&
            !fs::exists(s.root / "other.partial"),
        "cancelled: nothing installed, nothing partial left");
}

void TestTable() {
  bool xex = false;
  for (const GameFile& f : GameFiles()) {
    Check(f.sha256.size() == 64 && f.size > 0, "table entries are complete");
    if (f.path == kXexPath) {
      xex = f.sha256 == "87e4e1cd7b2eda9a1a6d8607609ecdb05474923ade81440475c2d8c2d55a4b6e";
    }
  }
  Check(xex, "the table's default.xex is version 1.0.140.0");
}

// With the user's own package (not in CI): extracts it and checks it against the real table.
int CheckRealPackage(const char* package) {
  const auto facts = ReadPackageFacts(package);
  Check(facts && CheckPackage(*facts).empty(), "the package is Torchlight XBLA");
  const fs::path game = fs::temp_directory_path() / "tl_game_setup_real";
  fs::remove_all(game);
  const auto result = Install(Source::kPackage, package, game, GameFiles(), {});
  std::printf("install: %s\n", result.error.empty() ? "ok" : Render(result.error).c_str());
  const bool ok = result.error.empty() && XexMatches(game, GameFiles());
  fs::remove_all(game);
  return ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 1) return CheckRealPackage(argv[1]);
  TestCheckPackage();
  TestReadPackageFacts();
  TestVerifyFolder();
  TestQuickCheck();
  TestInstallFromFolder();
  TestTable();
  std::printf("game_setup_test: ok\n");
  return 0;
}
