// The text data format, the mods folder scan, mods.dat and the registration plan, on synthetic
// mods written by the test into a temporary folder.
#include <cstdio>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "mods/dat_text.h"
#include "mods/mod_list.h"

namespace fs = std::filesystem;
using namespace torchlight::mods;

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
  if (!ok) {
    ++failures;
    std::fprintf(stderr, "FAIL: %s\n", what);
  }
}

std::vector<uint8_t> Ascii(const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); }

std::vector<uint8_t> Utf16(const std::string& ascii) {
  std::vector<uint8_t> out = {0xFF, 0xFE};
  for (char c : ascii) {
    out.push_back(static_cast<uint8_t>(c));
    out.push_back(0);
  }
  return out;
}

void Write(const fs::path& path, const std::vector<uint8_t>& bytes) {
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()),
                                               static_cast<std::streamsize>(bytes.size()));
}

std::string Descriptor(const std::string& name) {
  return "[MOD]\r\n\t<STRING>NAME:" + name + "\r\n\t<STRING>AUTHOR:Test Author\r\n"
         "\t<STRING>DESCRIPTION:A synthetic mod\r\n[/MOD]\r\n";
}
}  // namespace

int main() {
  // Text data format.
  {
    std::string error;
    const auto blocks = ParseDatText(
        Utf16("[TOP]\r\n\t<BOOL>FLAG:true\r\n\t[CHILD]\r\n\t\t<integer>COUNT:3\r\n\t[/CHILD]\r\n[/TOP]\r\n"), &error);
    Check(blocks && blocks->size() == 1, "UTF-16LE with BOM parses");
    if (blocks && !blocks->empty()) {
      const DatBlock& top = (*blocks)[0];
      Check(top.name == "TOP" && top.Find("flag") && top.Find("flag")->value == "true", "value, key case-insensitive");
      Check(top.children.size() == 1 && top.children[0].Find("COUNT")->type == "INTEGER", "nested block, type upper-cased");
    }
    Check(ParseDatText(Ascii("[A]\n<STRING>K:v:w\n[/A]\n"), &error).value()[0].Find("K")->value == "v:w",
          "UTF-8: the value keeps later colons");
    Check(!ParseDatText(Ascii("[A]\n"), &error), "unclosed block rejected");
    Check(!ParseDatText(Ascii("[A]\n[/B]\n"), &error), "mismatched closing tag rejected");
    Check(!ParseDatText(Ascii("<STRING>K:v\n"), &error), "value outside a block rejected");
    Check(!ParseDatText(Ascii("[A]\njunk\n[/A]\n"), &error), "stray line rejected");
    const std::vector<DatBlock> written = {DatBlock{"X", {{"STRING", "NAME", "Ünïcode"}}, {DatBlock{"Y", {}, {}}}}};
    const auto back = ParseDatText(WriteDatText(written), &error);
    Check(back && (*back)[0].Find("NAME")->value == "Ünïcode" && (*back)[0].children.size() == 1,
          "write then parse round-trips, non-ASCII included");
  }

  const fs::path root = fs::temp_directory_path() / ("tl_mods_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::remove_all(root);

  // Scan.
  Check(ScanModsFolder(root / "missing").mods.empty(), "missing mods folder: no mods");
  Write(root / "b_mod" / "mod.dat", Utf16(Descriptor("Mod B")));
  Write(root / "A_mod" / "MOD.DAT", Ascii(Descriptor("Mod A")));
  Write(root / "nodat" / "media" / "x.dds", Ascii("not a descriptor"));
  Write(root / "broken" / "mod.dat", Ascii("[MOD]\n<STRING>NAME:x\n"));
  Write(root / "loose.txt", Ascii("a file, not a folder"));
  ScanResult scan = ScanModsFolder(root);
  Check(scan.mods.size() == 2, "two mods found");
  if (scan.mods.size() == 2) {
    Check(scan.mods[0].folder == "A_mod" && scan.mods[1].folder == "b_mod", "folder-name order, case-insensitive");
    Check(scan.mods[0].name == "Mod A" && scan.mods[1].author == "Test Author", "descriptor fields read");
    Check(scan.mods[0].descriptor_digest != scan.mods[1].descriptor_digest, "digests differ");
  }
  Check(scan.skipped.size() == 2, "a folder without mod.dat and an unreadable one are skipped");

  // mods.dat.
  {
    std::string error;
    const auto list = ParseModList(
        Utf16("[MODS]\r\n<BOOL>CHECKFORNEW:false\r\n[MOD]\r\n<STRING>DIRECTORY:C:\\mods\\b_mod\\\r\n"
              "<INTEGER>PRIORITY:5\r\n<BOOL>COMPRESSED:1\r\n[/MOD]\r\n[/MODS]\r\n"),
        &error);
    Check(list && !list->check_for_new && list->entries.size() == 1, "PC-style list parses");
    if (list && list->entries.size() == 1) {
      Check(list->entries[0].priority == 5 && list->entries[0].compressed, "priority and compressed");
    }
    const auto again = ParseModList(WriteModList(*list), &error);
    Check(again && again->entries.size() == 1 && again->entries[0].directory == list->entries[0].directory,
          "written list reads back");
  }

  // Plan.
  {
    ModPlan plan = PlanMods(scan, std::nullopt);
    Check(plan.list_changed && plan.mods.size() == 2 && plan.mods[0].folder == "A_mod" &&
              plan.mods[0].priority == 1 && plan.mods[1].priority == 2,
          "no mods.dat: every mod added in folder order");

    Write(root / "c_mod" / "mod.dat", Ascii(Descriptor("Mod C")));
    Write(root / "d_mod" / "mod.dat", Ascii(Descriptor("Mod D")));
    scan = ScanModsFolder(root);
    ModList list;
    list.entries = {{"b_mod", 5, false}, {"gone_mod", 2, false}, {"A_mod", 3, false}, {"d_mod", -1, false},
                    {"B_MOD", 9, false}};
    plan = PlanMods(scan, list);
    Check(plan.list_changed, "a gone folder and a duplicate change the list");
    Check(plan.mods.size() == 4, "A, b, d listed; c added");
    if (plan.mods.size() == 4) {
      Check(plan.mods[0].folder == "A_mod" && plan.mods[1].folder == "b_mod" && plan.mods[2].folder == "c_mod",
            "enabled by priority, the new one after the highest");
      Check(plan.mods[2].priority == 6, "new mod: highest + 1");
      Check(plan.mods[3].folder == "d_mod" && plan.mods[3].priority == -1, "disabled mod registered last");
    }
    list.check_for_new = false;
    plan = PlanMods(scan, list);
    Check(plan.mods.size() == 3, "without CHECKFORNEW new folders are not added");

    // Fingerprint.
    Check(ModSetFingerprint(PlanMods(ScanResult{}, std::nullopt), ScanResult{}).empty(), "no mods: empty fingerprint");
    const std::string before = ModSetFingerprint(PlanMods(scan, std::nullopt), scan);
    Write(root / "A_mod" / "MOD.DAT", Ascii(Descriptor("Mod A, edited")));
    const ScanResult edited = ScanModsFolder(root);
    Check(ModSetFingerprint(PlanMods(edited, std::nullopt), edited) != before, "an edited mod.dat changes the fingerprint");
  }

  fs::remove_all(root);
  if (failures) return EXIT_FAILURE;
  std::puts("mods tests passed");
  return EXIT_SUCCESS;
}
