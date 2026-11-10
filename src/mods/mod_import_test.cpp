// The mods' import (mod_import.h) on synthetic zips and folders written by the test into a
// temporary folder: the layouts, the refused names and entries, the existing names, damaged zips,
// the cancel and the .partial cleanup.
#include <miniz.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "mods/mod_import.h"
#include "mods/mod_list.h"

namespace fs = std::filesystem;
using namespace torchlight::mods;
using Names = std::vector<std::string>;

namespace {
int failures = 0;
void Check(bool ok, const std::string& what) {
  if (!ok) {
    ++failures;
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
  }
}

struct ZipEntry {
  std::string name, content;
  bool ascii_name = false;  // no UTF-8 flag (the name is CP437 as the format says)
  bool symlink = false;     // a Unix symbolic link in the external attributes
  bool encrypted = false;   // the general purpose flag's bit 0
  bool absolute = false;    // the name's first byte made '/' (miniz's writer refuses one)
};

std::vector<uint8_t> ZipBytes(const std::vector<ZipEntry>& entries) {
  mz_zip_archive zip{};
  Check(mz_zip_writer_init_heap(&zip, 0, 0), "zip writer");
  for (const ZipEntry& e : entries) {
    const mz_uint flags = MZ_DEFAULT_COMPRESSION | (e.ascii_name ? MZ_ZIP_FLAG_ASCII_FILENAME : 0);
    Check(mz_zip_writer_add_mem_ex(&zip, e.name.c_str(), e.content.data(), e.content.size(),
                                   nullptr, 0, flags, 0, 0),
          "zip add " + e.name);
  }
  void* buffer = nullptr;
  size_t size = 0;
  mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size);
  std::vector<uint8_t> bytes(static_cast<uint8_t*>(buffer), static_cast<uint8_t*>(buffer) + size);
  mz_zip_writer_end(&zip);
  // The central directory's headers (signature PK\1\2): what miniz's writer cannot set.
  if (std::none_of(entries.begin(), entries.end(),
                   [](const ZipEntry& e) { return e.symlink || e.encrypted || e.absolute; })) {
    return bytes;
  }
  for (size_t at = 0; at + 30 <= bytes.size(); ++at) {
    // The local headers (PK\3\4) hold the name too, at 30.
    if (std::memcmp(&bytes[at], "PK\3\4", 4) == 0) {
      const size_t name_length = bytes[at + 26] | bytes[at + 27] << 8;
      const std::string name(reinterpret_cast<const char*>(&bytes[at + 30]), name_length);
      for (const ZipEntry& e : entries) {
        if (e.absolute && e.name == name) bytes[at + 30] = '/';
      }
      continue;
    }
    if (at + 46 > bytes.size()) break;
    if (std::memcmp(&bytes[at], "PK\1\2", 4) != 0) continue;
    const size_t name_length = bytes[at + 28] | bytes[at + 29] << 8;
    const std::string name(reinterpret_cast<const char*>(&bytes[at + 46]), name_length);
    for (const ZipEntry& e : entries) {
      if (e.name != name) continue;
      if (e.symlink) {
        bytes[at + 5] = 3;  // made by: Unix
        const uint32_t mode = 0120777u << 16;
        for (int b = 0; b < 4; ++b) bytes[at + 38 + b] = static_cast<uint8_t>(mode >> (8 * b));
      }
      if (e.encrypted) bytes[at + 8] |= 1;
      if (e.absolute) bytes[at + 46] = '/';
    }
  }
  return bytes;
}

void WriteBytes(const fs::path& path, const std::vector<uint8_t>& bytes) {
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary)
      .write(reinterpret_cast<const char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
}
void WriteText(const fs::path& path, const std::string& text) {
  WriteBytes(path, std::vector<uint8_t>(text.begin(), text.end()));
}
std::string ReadText(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), {});
}

// What the mods folder holds, names only.
std::vector<std::string> Listing(const fs::path& mods) {
  std::vector<std::string> names;
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(mods, ec)) {
    names.push_back(entry.path().filename().string());
  }
  std::sort(names.begin(), names.end());
  return names;
}

const std::string kModDat = "[MOD]\r\n\t<STRING>NAME:Synthetic\r\n[/MOD]\r\n";
}  // namespace

int main() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  const fs::path root = fs::temp_directory_path() / ("tl_mod_import_test_" + std::to_string(now));
  int run = 0;
  // A fresh data folder per case.
  auto fresh = [&]() {
    const fs::path data = root / ("data" + std::to_string(++run));
    fs::create_directories(data);
    return data;
  };
  auto zip = [&](const std::string& name, const std::vector<ZipEntry>& entries) {
    const fs::path path = root / "sources" / name;
    WriteBytes(path, ZipBytes(entries));
    return path;
  };

  // mod.dat at the root: one mod named after the zip; a mods.dat beside it is not copied.
  {
    const fs::path data = fresh();
    const auto result = ImportMod(zip("RootMod.zip", {{"mod.dat", kModDat},
                                                       {"mods.dat", "list"},
                                                       {"media/units/a.dat", "unit"},
                                                       {"media\\units\\b.dat", "unit b"}}),
                                  data);
    Check(result.ok() && result.mods == Names{"RootMod"}, "root: " + result.message());
    Check(ReadText(data / "mods/RootMod/mod.dat") == kModDat, "root: mod.dat copied");
    Check(ReadText(data / "mods/RootMod/media/units/a.dat") == "unit", "root: file copied");
    Check(ReadText(data / "mods/RootMod/media/units/b.dat") == "unit b",
          "root: backslash name as folders");
    Check(!fs::exists(data / "mods/RootMod/mods.dat"), "root: mods.dat left out");
    Check(Listing(data / "mods") == Names{"RootMod"}, "root: nothing else in mods");
  }

  // One top-level folder that is a mod (media/ only, no mod.dat), with a readme beside it.
  {
    const fs::path data = fresh();
    const auto result = ImportMod(zip("download-v2.zip", {{"readme.txt", "hello"},
                                                           {"FolderMod/", ""},
                                                           {"FolderMod/media/x.dds", "dds"}}),
                                  data);
    Check(result.ok() && result.mods == Names{"FolderMod"},
          "top folder: " + result.message());
    Check(ReadText(data / "mods/FolderMod/media/x.dds") == "dds", "top folder: file copied");
    Check(result.skipped == Names{"readme.txt"}, "top folder: readme skipped");
  }

  // A pack of two, with PC's mods.dat and a folder that is not a mod; and a zip of a mods folder.
  {
    const fs::path data = fresh();
    const auto result = ImportMod(zip("pack.zip", {{"mods.dat", "list"},
                                                    {"A/mod.dat", kModDat},
                                                    {"B/media/b.txt", "b"},
                                                    {"notes/n.txt", "n"}}),
                                  data);
    Check(result.ok() && result.mods == Names{"A", "B"}, "pack: " + result.message());
    Check(Listing(data / "mods") == Names{"A", "B"}, "pack: two folders, no mods.dat");
    Check(ReadText(data / "mods/B/media/b.txt") == "b", "pack: B's file");

    const fs::path data2 = fresh();
    const auto nested =
        ImportMod(zip("mods.zip", {{"mods/C/mod.dat", kModDat}, {"mods/D/media/d", "d"}}), data2);
    Check(nested.ok() && nested.mods == Names{"C", "D"},
          "mods folder zipped: " + nested.message());
  }

  // No mod.
  {
    const fs::path data = fresh();
    const auto result =
        ImportMod(zip("nothing.zip", {{"readme.txt", "x"}, {"pics/a.png", "p"}}), data);
    Check(result.status == ImportStatus::kNoModFound, "no mod: " + result.message());
    Check(Listing(data / "mods").empty(), "no mod: nothing written");
  }

  // Refused names and entries; nothing written for any.
  {
    const struct {
      const char* zip;
      ZipEntry bad;
      ImportStatus status;
    } cases[] = {
        {"slip.zip", {"Mod/../../evil.txt", "x"}, ImportStatus::kUnsafeName},
        {"slip2.zip", {"Mod\\..\\..\\evil.txt", "x"}, ImportStatus::kUnsafeName},
        {"drive.zip", {"C:/evil.txt", "x"}, ImportStatus::kUnsafeName},
        {"stream.zip", {"Mod/media/a.dds:hidden", "x"}, ImportStatus::kUnsafeName},
        {"link.zip", {"Mod/media/link", "/etc/passwd", false, true},
         ImportStatus::kUnsupportedEntry},
        {"locked.zip", {"Mod/media/secret", "x", false, false, true},
         ImportStatus::kUnsupportedEntry},
        {"cp437.zip", {"Mod/media/caf\xE9.dds", "x", true}, ImportStatus::kNonAsciiName},
        {"badutf8.zip", {"Mod/media/caf\xE9.dds", "x"}, ImportStatus::kNonAsciiName},
        {"absolute.zip", {"_evil", "x", false, false, false, true}, ImportStatus::kUnsafeName},
        {"device.zip", {"Mod/media/COM1", "x"}, ImportStatus::kUnsafeName},
        {"device2.zip", {"Mod/media/nul.txt", "x"}, ImportStatus::kUnsafeName},
        {"device3.zip", {"Mod/Lpt9/a.dds", "x"}, ImportStatus::kUnsafeName},
        {"device4.zip", {"Mod/media/aux .dds", "x"}, ImportStatus::kUnsafeName},
        {"chars.zip", {"Mod/media/a?.dds", "x"}, ImportStatus::kUnsafeName},
        {"chars2.zip", {"Mod/media/a<b>.dds", "x"}, ImportStatus::kUnsafeName},
        {"chars3.zip", {"Mod/media/a|b\"c*.dds", "x"}, ImportStatus::kUnsafeName},
        {"trail.zip", {"Mod/media/dot.", "x"}, ImportStatus::kUnsafeName},
        {"trail2.zip", {"Mod/media /a.dds", "x"}, ImportStatus::kUnsafeName},
        {"longname.zip", {"Mod/media/" + std::string(1100, 'a'), "x"}, ImportStatus::kUnreadable},
    };
    for (const auto& c : cases) {
      const fs::path data = fresh();
      const auto result = ImportMod(zip(c.zip, {{"Mod/mod.dat", kModDat}, c.bad}), data);
      Check(result.status == c.status, std::string(c.zip) + ": " + result.message());
      Check(Listing(data / "mods").empty(), std::string(c.zip) + ": nothing written");
      Check(!fs::exists(data / "evil.txt") && !fs::exists(root / "evil.txt"),
            std::string(c.zip) + ": nothing outside");
    }
    // UTF-8 names with the flag are kept.
    const fs::path data = fresh();
    const auto result =
        ImportMod(zip("utf8.zip", {{"Mod/mod.dat", kModDat}, {"Mod/media/caf\xC3\xA9.dds", "x"}}),
                  data);
    Check(result.ok() && fs::exists(data / "mods/Mod/media" / fs::path(u8"café.dds")),
          "utf-8 name: " + result.message());
  }

  // A name already installed (in another case): nothing replaced, nothing of the pack installed.
  {
    const fs::path data = fresh();
    WriteText(data / "mods/b/mine.txt", "mine");
    const auto result =
        ImportMod(zip("pack2.zip", {{"A/mod.dat", kModDat}, {"B/mod.dat", kModDat}}), data);
    Check(result.status == ImportStatus::kAlreadyInstalled && result.mods == Names{"B"},
          "existing: " + result.message());
    Check(Listing(data / "mods") == Names{"b"} &&
              ReadText(data / "mods/b/mine.txt") == "mine",
          "existing: untouched, A not installed");
  }

  // Replacing (the player confirmed): the old folder goes only once the new one is in place.
  {
    const fs::path data = fresh();
    WriteText(data / "mods/b/old.txt", "old");
    WriteText(data / "mods/Keep/mod.dat", kModDat);
    const fs::path source = zip("replace.zip", {{"A/mod.dat", kModDat}, {"B/mod.dat", kModDat}, {"B/new.txt", "new"}});
    ImportOptions replace;
    replace.replace = true;
    const auto result = ImportMod(source, data, {}, nullptr, replace);
    Check(result.ok() && result.mods == Names{"A", "B"} && result.replaced == Names{"b"},
          "replace: " + result.message());
    Check(Listing(data / "mods") == Names{"A", "B", "Keep"} && ReadText(data / "mods/B/new.txt") == "new" &&
              !fs::exists(data / "mods/B/old.txt"),
          "replace: the new B in place, the old b's files gone, Keep untouched");
    Check(result.message().find("replacing b") != std::string::npos, "replace: the log names it");
  }

  // A replace that fails (a pack whose second mod's data is damaged): the old mods byte-identical,
  // nothing of the new ones left.
  {
    const fs::path data = fresh();
    WriteText(data / "mods/A/mod.dat", "old A");
    WriteText(data / "mods/B/media/b.txt", "old B");
    auto bytes = ZipBytes({{"A/mod.dat", kModDat}, {"B/mod.dat", kModDat}, {"B/media/b.txt", std::string(4000, 'b')}});
    size_t third = 0, count = 0;
    for (size_t at = 0; at + 4 <= bytes.size(); ++at) {
      if (std::memcmp(&bytes[at], "PK\3\4", 4) == 0 && ++count == 3) third = at;
    }
    Check(third > 0, "replace failing: third entry found");
    const size_t data_at = third + 30 + std::strlen("B/media/b.txt");
    for (size_t i = data_at; i < data_at + 4 && i < bytes.size(); ++i) bytes[i] ^= 0x5A;
    WriteBytes(root / "sources/replace-damaged.zip", bytes);
    ImportOptions replace;
    replace.replace = true;
    const auto result = ImportMod(root / "sources/replace-damaged.zip", data, {}, nullptr, replace);
    Check(result.status == ImportStatus::kUnreadable && result.replaced.empty(),
          "replace failing: " + result.message());
    Check(Listing(data / "mods") == Names{"A", "B"} && ReadText(data / "mods/A/mod.dat") == "old A" &&
              ReadText(data / "mods/B/media/b.txt") == "old B" && !fs::exists(data / "mods/B/mod.dat"),
          "replace failing: the old mods intact");
  }

  // Without the player's confirmation an existing name still fails, as before.
  {
    const fs::path data = fresh();
    WriteText(data / "mods/Solo/mod.dat", "old");
    const auto result = ImportMod(zip("Solo.zip", {{"mod.dat", kModDat}}), data);
    Check(result.status == ImportStatus::kAlreadyInstalled && result.mods == Names{"Solo"} &&
              ReadText(data / "mods/Solo/mod.dat") == "old",
          "no replace: refused, untouched");
  }

  // What an interrupted replace left: a .replaced whose mod is missing comes back; one whose mod is
  // there goes. Then the import goes on.
  {
    const fs::path data = fresh();
    WriteText(data / "mods/Lost.replaced/mod.dat", "lost");
    WriteText(data / "mods/Done.replaced/mod.dat", "stale");
    WriteText(data / "mods/Done/mod.dat", "done");
    const auto result = ImportMod(zip("Other.zip", {{"mod.dat", kModDat}}), data);
    Check(result.ok(), "recovery: " + result.message());
    Check(Listing(data / "mods") == Names{"Done", "Lost", "Other"} && ReadText(data / "mods/Lost/mod.dat") == "lost" &&
              ReadText(data / "mods/Done/mod.dat") == "done",
          "recovery: Lost put back, Done's old copy removed");
  }

  // A swap that fails on the second mod of a pack (a fault before it is renamed into place): the
  // first, already swapped, goes aside, both old mods come back byte-identical, nothing new left.
  {
    const fs::path data = fresh();
    WriteText(data / "mods/A/mod.dat", "old A");
    WriteText(data / "mods/B/mod.dat", "old B");
    ImportOptions replace;
    replace.replace = true;
    replace.fault = [](const std::string& step) { return step == "swap:B"; };
    const auto result = ImportMod(zip("swap-fail.zip", {{"A/mod.dat", kModDat}, {"B/mod.dat", kModDat}}), data, {},
                                  nullptr, replace);
    Check(result.status == ImportStatus::kCannotWrite && result.mods.empty() && result.replaced.empty(),
          "swap failing: " + result.message());
    Check(Listing(data / "mods") == Names{"A", "B"} && ReadText(data / "mods/A/mod.dat") == "old A" &&
              ReadText(data / "mods/B/mod.dat") == "old B",
          "swap failing: the old mods back, nothing else left");
  }

  // An undo that cannot put an old mod back: reported, the old one kept as .replaced, the new one
  // (whole) in its place; the recovery then keeps the mod that is there.
  {
    const fs::path data = fresh();
    WriteText(data / "mods/A/mod.dat", "old A");
    WriteText(data / "mods/B/mod.dat", "old B");
    ImportOptions replace;
    replace.replace = true;
    replace.fault = [](const std::string& step) { return step == "swap:B" || step == "undo:A"; };
    const auto result = ImportMod(zip("undo-fail.zip", {{"A/mod.dat", kModDat}, {"B/mod.dat", kModDat}}), data, {},
                                  nullptr, replace);
    Check(result.status == ImportStatus::kCannotWrite && result.detail.find("A.replaced") != std::string::npos,
          "undo failing: reported with the backup's name: " + result.message());
    Check(Listing(data / "mods") == Names{"A", "A.replaced", "B"} && ReadText(data / "mods/A/mod.dat") == kModDat &&
              ReadText(data / "mods/A.replaced/mod.dat") == "old A" && ReadText(data / "mods/B/mod.dat") == "old B",
          "undo failing: A whole (new), its old copy kept, B old");
  }
  {
    // The restore itself failing (the new one moved aside): the old one kept, nothing lost; the
    // recovery puts it back.
    const fs::path data = fresh();
    WriteText(data / "mods/A/mod.dat", "old A");
    ImportOptions replace;
    replace.replace = true;
    replace.fault = [](const std::string& step) { return step == "swap:B" || step == "restore:A"; };
    WriteText(data / "mods/B/mod.dat", "old B");
    const auto result = ImportMod(zip("restore-fail.zip", {{"A/mod.dat", kModDat}, {"B/mod.dat", kModDat}}), data,
                                  {}, nullptr, replace);
    Check(result.status == ImportStatus::kCannotWrite && result.detail.find("A.replaced") != std::string::npos,
          "restore failing: reported: " + result.message());
    Check(Listing(data / "mods") == Names{"A.replaced", "B"} && ReadText(data / "mods/A.replaced/mod.dat") == "old A",
          "restore failing: the old A kept aside, the new one removed");
    const ImportRecovery recovery = RecoverInterruptedImports(data / "mods");
    Check(recovery.ok() && recovery.restored == Names{"A"} && ReadText(data / "mods/A/mod.dat") == "old A",
          "restore failing: the recovery puts A back");
  }

  // Killed between the old mod's move aside and the new one's rename: .replaced and .partial; the
  // recovery (as at start-up) puts the old one back and drops the partial; the scan then sees it.
  {
    const fs::path data = fresh();
    WriteText(data / "mods/X.replaced/mod.dat", kModDat);
    WriteText(data / "mods/X.replaced/media/old.txt", "old X");
    WriteText(data / "mods/X.partial/mod.dat", kModDat);
    WriteText(data / "mods/Y.undo.partial/mod.dat", "aside");
    const ImportRecovery recovery = RecoverInterruptedImports(data / "mods");
    Check(recovery.ok() && recovery.restored == Names{"X"} && recovery.removed.size() == 2,
          "mid-swap: X back, the partials removed");
    Check(Listing(data / "mods") == Names{"X"} && ReadText(data / "mods/X/media/old.txt") == "old X",
          "mid-swap: only the old X");
    const ScanResult scan = ScanModsFolder(data / "mods");
    Check(scan.mods.size() == 1 && scan.mods[0].folder == "X", "mid-swap: the start-up scan sees X");
  }

  // Case variants: the old "mymod" replaced by "MyMod", killed before the old one was removed. The
  // recovery removes the old copy (the mod is there in another case), never making two.
  {
    const fs::path data = fresh();
    WriteText(data / "mods/MyMod/mod.dat", "new");
    WriteText(data / "mods/mymod.replaced/mod.dat", "old");
    const ImportRecovery recovery = RecoverInterruptedImports(data / "mods");
    Check(recovery.ok() && recovery.restored.empty() && recovery.removed == Names{"mymod.replaced"},
          "case variant: the old copy removed");
    Check(Listing(data / "mods") == Names{"MyMod"}, "case variant: one mod");
    Check(RecoverInterruptedImports(data / "nowhere").ok(), "no mods folder: nothing to do");
  }

  // Damaged zips: truncated (no central directory), not a zip, and an entry whose data is
  // damaged after the first was written (its .partial removed).
  {
    const fs::path data = fresh();
    auto bytes = ZipBytes({{"Mod/mod.dat", kModDat}, {"Mod/media/a", std::string(4000, 'a')}});
    auto truncated = bytes;
    truncated.resize(truncated.size() / 2);
    WriteBytes(root / "sources/truncated.zip", truncated);
    auto result = ImportMod(root / "sources/truncated.zip", data);
    Check(result.status == ImportStatus::kUnreadable, "truncated: " + result.message());
    WriteText(root / "sources/text.zip", "not a zip at all");
    result = ImportMod(root / "sources/text.zip", data);
    Check(result.status == ImportStatus::kNotAZip && result.detail == "text.zip", "not a zip: " + result.message());
    WriteText(root / "sources/archive.rar", "Rar!\x1a\x07\x01");
    result = ImportMod(root / "sources/archive.rar", data);
    Check(result.status == ImportStatus::kNotAZip, "a .rar: " + result.message());
    WriteText(root / "sources/empty.zip", "");
    result = ImportMod(root / "sources/empty.zip", data);
    Check(result.status == ImportStatus::kNotAZip, "an empty file: " + result.message());
    // The second entry's data: flip bytes in the middle of the file (after mod.dat's local entry).
    auto damaged =
        ZipBytes({{"Mod/mod.dat", kModDat}, {"Mod/media/a", std::string(4000, 'a') + "end"}});
    const size_t second = [&] {
      size_t found = 0, count = 0;
      for (size_t at = 0; at + 4 <= damaged.size(); ++at) {
        if (std::memcmp(&damaged[at], "PK\3\4", 4) == 0 && ++count == 2) found = at;
      }
      return found;
    }();
    Check(second > 0, "damaged: second entry found");
    const size_t data_at = second + 30 + std::strlen("Mod/media/a");
    for (size_t i = data_at; i < data_at + 4 && i < damaged.size(); ++i) damaged[i] ^= 0x5A;
    WriteBytes(root / "sources/damaged.zip", damaged);
    result = ImportMod(root / "sources/damaged.zip", data);
    Check(result.status == ImportStatus::kUnreadable, "damaged entry: " + result.message());
    Check(Listing(data / "mods").empty(), "damaged entry: no mod and no .partial left");
  }

  // A folder as the source: copied as it is; a symbolic link in it is refused.
  {
    const fs::path data = fresh();
    const fs::path source = root / "sources/Plain Folder Mod";
    WriteText(source / "MOD.DAT", kModDat);
    WriteText(source / "media/units/u.dat", "u");
    fs::create_directories(source / "media/empty");
    ImportProgress last;
    const auto result = ImportMod(source, data, [&](const ImportProgress& p) { last = p; });
    Check(result.ok() && result.mods == Names{"Plain Folder Mod"},
          "folder: " + result.message());
    Check(ReadText(data / "mods/Plain Folder Mod/media/units/u.dat") == "u", "folder: file copied");
    Check(fs::is_directory(data / "mods/Plain Folder Mod/media/empty"),
          "folder: empty folder kept");
    Check(last.files_done == 2 && last.files_total == 2 && last.bytes_done == last.bytes_total &&
              last.bytes_total == kModDat.size() + 1,
          "folder: progress counts files and bytes");

    std::error_code ec;
    fs::create_symlink("/etc/hostname", source / "media/link", ec);
    if (!ec) {
      const fs::path data2 = fresh();
      const auto linked = ImportMod(source, data2);
      Check(linked.status == ImportStatus::kUnsupportedEntry, "folder link: " + linked.message());
      Check(Listing(data2 / "mods").empty(), "folder link: nothing written");
    }
  }

  // Cancel after the first file: nothing left, not even the .partial. A stale .partial from an
  // interrupted import is replaced by the next one.
  {
    const fs::path data = fresh();
    const fs::path source =
        zip("Cancel.zip", {{"mod.dat", kModDat}, {"media/a", "a"}, {"media/b", "b"}});
    std::atomic<bool> cancel{false};
    const auto result = ImportMod(
        source, data, [&](const ImportProgress& p) { cancel = p.files_done >= 1; }, &cancel);
    Check(result.status == ImportStatus::kCancelled && result.mods.empty(),
          "cancel: " + result.message());
    Check(Listing(data / "mods").empty(), "cancel: nothing left");

    WriteText(data / "mods/Cancel.partial/stale.txt", "old");
    const auto again = ImportMod(source, data);
    Check(again.ok(), "after cancel: " + again.message());
    Check(Listing(data / "mods") == Names{"Cancel"} &&
              !fs::exists(data / "mods/Cancel/stale.txt"),
          "after cancel: the stale .partial is not part of the mod");
  }

  // Names alike but for case: two mods of a pack, or two files of a mod; and names that only look
  // like a device (COM0, CONSOLE, a .com file) are fine.
  {
    const fs::path data = fresh();
    auto result = ImportMod(zip("case.zip", {{"A/mod.dat", kModDat}, {"a/media/x", "x"}}), data);
    Check(result.status == ImportStatus::kUnsafeName, "A/ and a/: " + result.message());
    result = ImportMod(zip("case2.zip", {{"mod.dat", kModDat}, {"media/X.dds", "1"}, {"Media/x.DDS", "2"}}),
                       data);
    Check(result.status == ImportStatus::kUnsafeName, "files alike but for case: " + result.message());
    Check(Listing(data / "mods").empty(), "case: nothing written");
    result = ImportMod(
        zip("near.zip", {{"mod.dat", kModDat}, {"media/COM0", "a"}, {"media/CONSOLE.txt", "b"}, {"media/x.com", "c"}}),
        data);
    Check(result.ok(), "names near a device's: " + result.message());
  }

  // An entry whose data is larger than its header says: refused, nothing left.
  {
    const fs::path data = fresh();
    auto bytes = ZipBytes({{"Mod/mod.dat", kModDat}, {"Mod/media/big", std::string(8000, 'b')}});
    // The central directory's uncompressed size (offset 24) of the second entry, made 100.
    int seen = 0;
    for (size_t at = 0; at + 46 <= bytes.size(); ++at) {
      if (std::memcmp(&bytes[at], "PK\1\2", 4) != 0 || ++seen != 2) continue;
      bytes[at + 24] = 100;
      bytes[at + 25] = bytes[at + 26] = bytes[at + 27] = 0;
    }
    WriteBytes(root / "sources/larger.zip", bytes);
    const auto result = ImportMod(root / "sources/larger.zip", data);
    Check(result.status == ImportStatus::kUnreadable, "larger than declared: " + result.message());
    Check(Listing(data / "mods").empty(), "larger than declared: nothing left");
  }

  // Many empty entries: imported, each counted; past the cap: refused before reading them.
  {
    const fs::path data = fresh();
    std::vector<ZipEntry> many = {{"mod.dat", kModDat}};
    for (int i = 0; i < 3000; ++i) many.push_back({"media/e" + std::to_string(i), ""});
    ImportProgress last;
    const auto result = ImportMod(zip("Many.zip", many), data, [&](const ImportProgress& p) { last = p; });
    Check(result.ok() && last.files_total == 3001 && last.files_done == 3001, "many empty: " + result.message());

    std::vector<ZipEntry> too_many;
    too_many.reserve(kMaxImportEntries + 1);
    for (size_t i = 0; i <= kMaxImportEntries; ++i) too_many.push_back({"m/" + std::to_string(i), ""});
    const fs::path data2 = fresh();
    const auto capped = ImportMod(zip("TooMany.zip", too_many), data2);
    Check(capped.status == ImportStatus::kUnreadable, "past the cap: " + capped.message());
    Check(Listing(data2 / "mods").empty(), "past the cap: nothing written");
  }

  // The space: blocks and the reserve.
  {
    Check(ImportSpaceNeeded({}, 4096) == 0, "space: nothing");
    Check(ImportSpaceNeeded({0, 1, 4096, 4097}, 4096) == 4096 + 4096 + 4096 + 8192, "space: rounded to blocks");
    Check(ImportSpaceNeeded({UINT64_MAX - 10, 4096}, 4096) == UINT64_MAX &&
              ImportSpaceNeeded({UINT64_MAX / 2, UINT64_MAX / 2, UINT64_MAX / 2}, 4096) == UINT64_MAX,
          "space: absurd sizes saturate instead of wrapping");
    Check(ImportSpaceReserve(1ull << 30) == kImportReserveMin, "reserve: at least 256 MB");
    Check(ImportSpaceReserve(100ull << 30) == 5ull << 30, "reserve: 5 % of a large disk");
  }

  // A cancel already set stops the listing: nothing written.
  {
    const fs::path data = fresh();
    std::atomic<bool> cancel{true};
    const auto result = ImportMod(zip("Early.zip", {{"mod.dat", kModDat}}), data, {}, &cancel);
    Check(result.status == ImportStatus::kCancelled && Listing(data / "mods").empty(),
          "cancel while listing: " + result.message());
  }

  // Every .partial in the mods folder goes when an import starts; a folder with a name Windows
  // refuses is refused as a source too.
  {
    const fs::path data = fresh();
    WriteText(data / "mods/Other.partial/old.txt", "old");
    const auto result = ImportMod(zip("Fresh.zip", {{"mod.dat", kModDat}}), data);
    Check(result.ok() && Listing(data / "mods") == Names{"Fresh"}, "stale partials cleared: " + result.message());

    // (Only where the file system lets such a name be made: not on Windows.)
    const fs::path source = root / "sources/Bad Folder";
    WriteText(source / "mod.dat", kModDat);
    WriteText(source / "media/what?.dds", "x");
    std::error_code made;
    if (fs::exists(source / "media/what?.dds", made)) {
      const fs::path data2 = fresh();
      const auto folder = ImportMod(source, data2);
      Check(folder.status == ImportStatus::kUnsafeName, "folder with a '?': " + folder.message());
    }
  }

  // Names the mod cannot take.
  {
    const fs::path data = fresh();
    const auto result = ImportMod(zip("x.partial.zip", {{"mod.dat", kModDat}}), data);
    Check(result.status == ImportStatus::kUnsafeName, "partial name: " + result.message());
    const auto replaced = ImportMod(zip("x.Replaced.zip", {{"mod.dat", kModDat}}), data);
    Check(replaced.status == ImportStatus::kUnsafeName, "replaced name: " + replaced.message());
  }

  std::error_code ec;
  fs::remove_all(root, ec);
  if (failures) {
    std::fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
  }
  std::puts("mod_import_test: ok");
  return 0;
}
