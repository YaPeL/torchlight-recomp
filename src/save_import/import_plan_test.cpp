// Tests for the import folder flow: scanning, the plan (numbers, rejections, the stash and
// settings decisions), "Yes" and "do not ask again", and applying the pending part at the next
// start, with temporary folders and the synthetic fixtures (no game data).

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include "save_import/import_plan.h"
#include "save_import/stash.h"

namespace {

using namespace torchlight::save_import;
namespace fs = std::filesystem;

void Check(bool ok, const std::string& what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    std::exit(1);
  }
}

fs::path FixturePath(const std::string& name) { return fs::path(SAVE_IMPORT_FIXTURES) / name; }

Bytes Read(const fs::path& path) {
  std::ifstream file(path, std::ios::binary);
  return Bytes(std::istreambuf_iterator<char>(file), {});
}

void Write(const fs::path& path, const Bytes& data) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

std::vector<std::string> Names(const fs::path& folder) {
  std::vector<std::string> names;
  for (const auto& entry : fs::directory_iterator(folder)) names.push_back(entry.path().filename().string());
  std::sort(names.begin(), names.end());
  return names;
}

// A temporary import folder and save container.
struct Scene {
  fs::path root, import, container;
  std::vector<std::string> log;

  Scene() {
    std::random_device random;
    root = fs::temp_directory_path() / ("save_import_test_" + std::to_string(random()));
    import = root / "import";
    container = root / "torchlight.sav";
    fs::create_directories(import);
    fs::create_directories(container);
  }
  ~Scene() {
    std::error_code ec;
    fs::remove_all(root, ec);
  }
  void Copy(const std::string& fixture, const fs::path& to) { fs::copy_file(FixturePath(fixture), to); }
  ContainerView View() const {
    ContainerView view;
    for (const auto& entry : fs::directory_iterator(container)) {
      const std::string name = entry.path().filename().string();
      view.file_names.push_back(name);
      if (name == "sharedstash.bin" || name == "sharedstashh.bin") view.stashes[name] = Read(entry.path());
      if (name == "settings.txt" || name == "local_settings.txt") view.settings[name] = Read(entry.path());
    }
    return view;
  }
  Log Logger() {
    return [this](const std::string& line) { log.push_back(line); };
  }
};

std::unique_ptr<Schema> TheSchema() {
  std::string error;
  auto schema = Schema::Embedded(error);
  Check(schema != nullptr, "schema: " + error);
  return schema;
}

void TestFreeNumbers() {
  Check(FreeCharacterNumber({}, {}) == 0, "empty container");
  Check(FreeCharacterNumber({"0.TSV", "1.tsv", "3.TSV"}, {}) == 2, "first gap");
  Check(FreeCharacterNumber({"0.TSV", "1.tsv", "3.TSV"}, {2}) == 4, "also taken");
  Check(FreeCharacterNumber({"abc.tsv"}, {}) == 1, "no digits reads as 0, like wcstol");
  Check(FreeCharacterNumber({" 0x.tsv", "1_TSV", "backup.tmp"}, {}) == 1, "leading space and digits");
  Check(FreeCharacterNumber({"1_TSV"}, {}) == 0, "not a .tsv");
}

void TestScan() {
  Scene scene;
  for (const char* name : {"0.SVT", "1.svt", "0.SVT.bkp", "2.svt.skipped", "SHAREDSTASH.BIN",
                           "SETTINGS.TXT", "local_settings.txt", "Pak.zip", "notes.txt"}) {
    Write(scene.import / name, {1});
  }
  const ImportFolder folder = ScanImportFolder(scene.import);
  Check(folder.characters.size() == 2, "two characters");
  Check(folder.stashes.size() == 1 && folder.settings.size() == 2, "stash and settings");
  Check(folder.settings[0].filename() == "SETTINGS.TXT", "settings.txt searched first");
  Check(folder.pc_pak && folder.pc_pak->filename() == "Pak.zip", "PC pak found whatever its case");
}

void TestMissingPak(const Schema& schema) {
  Scene scene;
  scene.Copy("pc_save.svt", scene.import / "0.SVT");
  const ImportFolder folder = ScanImportFolder(scene.import);
  const ImportPlan plan = BuildImportPlan(schema, folder, FixturePath("pak_360.zip"), scene.View());
  Check(plan.missing_pc_pak && !plan.blocked.empty() && plan.HasSomethingToAsk(), "blocked without Pak.zip");
  std::string error;
  Check(SkipImport(plan, folder, scene.Logger(), error), "skip: " + error);
  Check(Names(scene.import) == std::vector<std::string>{"0.SVT.skipped"}, "skipped, not deleted");
  Check(!ScanImportFolder(scene.import).HasWork(), "not asked again");
}

void TestCharacters(const Schema& schema) {
  Scene scene;
  scene.Copy("pak_pc.zip", scene.import / "Pak.zip");
  scene.Copy("pc_save.svt", scene.import / "3.SVT");
  scene.Copy("quest_save.svt", scene.import / "0.svt");
  for (const char* name : {"0.TSV", "1.tsv", "3.TSV"}) Write(scene.container / name, {1});
  const ImportFolder folder = ScanImportFolder(scene.import);
  const ImportPlan plan = BuildImportPlan(schema, folder, FixturePath("pak_360.zip"), scene.View());
  Check(plan.blocked.empty(), "not blocked: " + plan.blocked);
  Check(plan.characters.size() == 2, "two characters");
  Check(plan.characters[0].source.filename() == "0.svt" && plan.characters[0].number == 2,
        "0.svt gets the first free number");
  Check(plan.characters[1].number == 4, "3.SVT the next one");
  Check(plan.characters[0].ok() && plan.characters[0].converted == Read(FixturePath("quest_save.expected.tsv")),
        "converted like the Python tool");
  Check(!plan.characters[0].changes.empty(), "dialog adaptation reported");

  // The caller writes them through the game's mount; then "Yes" renames the sources.
  for (const auto& character : plan.characters) Write(scene.container / character.destination(), character.converted);
  std::string error;
  Check(ConfirmImport(plan, folder, scene.container, {}, scene.Logger(), error), "confirm: " + error);
  Check(fs::exists(scene.import / "0.svt.bkp") && fs::exists(scene.import / "3.SVT.bkp"), "renamed .bkp");
  Check(!ScanImportFolder(scene.import).HasWork(), "nothing left to ask");
  Check(!fs::exists(scene.import / kPendingFolder), "nothing pending");
}

void TestRejected(const Schema& schema) {
  Scene scene;
  scene.Copy("pak_pc.zip", scene.import / "Pak.zip");
  scene.Copy("pc_save.svt", scene.import / "0.SVT");
  const ImportFolder folder = ScanImportFolder(scene.import);
  const ImportPlan plan = BuildImportPlan(schema, folder, FixturePath("pak_360_missing_unit.zip"), scene.View());
  Check(plan.characters.size() == 1 && !plan.characters[0].ok(), "rejected");
  // That pak lacks a unit the save's items, pet and creatures use: not a reason any more (they go
  // when imported); its quest dialogs differ in a way that cannot be adapted: that one is.
  const auto& problems = plan.characters[0].problems;
  Check(std::none_of(problems.begin(), problems.end(),
                     [](const std::string& p) { return p.find("unit (UNIT_GUID)") != std::string::npos; }) &&
            std::any_of(problems.begin(), problems.end(),
                        [](const std::string& p) { return p.find("dialog") != std::string::npos; }),
        "says why");
  const auto& changes = plan.characters[0].changes;
  Check(std::any_of(changes.begin(), changes.end(),
                    [](const std::string& c) { return c.starts_with("removed when imported"); }),
        "the missing unit's entries reported as removed");
  std::string error;
  Check(ConfirmImport(plan, folder, scene.container, {}, scene.Logger(), error), "confirm: " + error);
  Check(fs::exists(scene.import / "0.SVT.rejected"), "renamed .rejected");
  Check(std::any_of(scene.log.begin(), scene.log.end(),
                    [](const std::string& l) { return l.find("dialog") != std::string::npos; }),
        "reason logged");
}

// The character's own class missing in the 360 data: refused, whatever its items.
void TestClassMissing(const Schema& schema) {
  Scene scene;
  scene.Copy("pak_pc.zip", scene.import / "Pak.zip");
  SaveError save_error;
  auto pc = ReadPc(schema, Read(FixturePath("pc_save.svt")), save_error);
  Check(pc != nullptr, "fixture: " + save_error.message);
  pc->tree.Find("player")->Find("unit_guid")->number = 0x0777000000000002ull;
  Bytes body = WriteBody(schema, *schema.root(), pc->tree, Endian::kLittle);
  const uint32_t size = static_cast<uint32_t>(body.size() + 4);
  for (int i = 0; i < 4; ++i) body.push_back(static_cast<uint8_t>(size >> (8 * i)));
  Write(scene.import / "0.SVT", body);
  const ImportPlan plan = BuildImportPlan(schema, ScanImportFolder(scene.import), FixturePath("pak_360.zip"), scene.View());
  const auto& problems = plan.characters[0].problems;
  Check(plan.characters.size() == 1 && !plan.characters[0].ok() &&
            std::any_of(problems.begin(), problems.end(),
                        [](const std::string& p) { return p.find("at player/unit_guid") != std::string::npos; }),
        "class missing: refused, and says why");
}

// A PC character holding an item of a unit the 360 game lacks (a PC mod's): imported, the item
// reported; the recomp takes it out of the save at the start that applies the import
// (mods/save_units.h).
void TestItemOfMissingUnit(const Schema& schema) {
  Scene scene;
  scene.Copy("pak_pc.zip", scene.import / "Pak.zip");
  SaveError save_error;
  auto pc = ReadPc(schema, Read(FixturePath("pc_save.svt")), save_error);
  Check(pc != nullptr, "fixture: " + save_error.message);
  Node* item = &pc->tree.Find("player")->Find("items")->items.front();
  item->Find("unit_guid")->number = 0x0777000000000001ull;
  Bytes body = WriteBody(schema, *schema.root(), pc->tree, Endian::kLittle);
  const uint32_t size = static_cast<uint32_t>(body.size() + 4);
  for (int i = 0; i < 4; ++i) body.push_back(static_cast<uint8_t>(size >> (8 * i)));
  Write(scene.import / "0.SVT", body);
  const ImportFolder folder = ScanImportFolder(scene.import);
  const ImportPlan plan = BuildImportPlan(schema, folder, FixturePath("pak_360.zip"), scene.View());
  Check(plan.characters.size() == 1 && plan.characters[0].ok(), "imported");
  const auto& changes = plan.characters[0].changes;
  Check(std::any_of(changes.begin(), changes.end(),
                    [](const std::string& c) { return c.starts_with("removed when imported") &&
                                                      c.find("player/items/item/unit_guid") != std::string::npos; }),
        "the item reported as removed when imported");
}

void TestStashReplaced(const Schema& schema) {
  Scene scene;
  scene.Copy("pak_pc.zip", scene.import / "Pak.zip");
  scene.Copy("pc_stash.bin", scene.import / "SHAREDSTASH.BIN");
  scene.Copy("pc_stash.expected.bin", scene.container / "sharedstash.bin");  // 1 item already
  const Bytes recomp_before = Read(scene.container / "sharedstash.bin");

  ImportFolder folder = ScanImportFolder(scene.import);
  ImportPlan plan = BuildImportPlan(schema, folder, FixturePath("pak_360.zip"), scene.View());
  Check(plan.stashes.size() == 1 && plan.stashes[0].needs_replace_confirmation() &&
            plan.stashes[0].recomp_items == 1,
        "asks before replacing a stash with items");

  // Not accepted: nothing pending, the PC stash stays to be asked about again.
  std::string error;
  Check(ConfirmImport(plan, folder, scene.container, {}, scene.Logger(), error), "confirm: " + error);
  Check(!fs::exists(scene.import / kPendingFolder / "sharedstash.bin"), "not pending");
  Check(ScanImportFolder(scene.import).stashes.size() == 1, "asked again later");

  // Accepted: pending, then applied at the next start, with the old one kept.
  folder = ScanImportFolder(scene.import);
  plan = BuildImportPlan(schema, folder, FixturePath("pak_360.zip"), scene.View());
  Check(ConfirmImport(plan, folder, scene.container, {{"sharedstash.bin", true}}, scene.Logger(), error),
        "confirm: " + error);
  Check(fs::exists(scene.import / kPendingFolder / "sharedstash.bin"), "pending");
  Check(ScanImportFolder(scene.import).stashes.empty(), "not asked while pending");
  Check(Read(scene.container / "sharedstash.bin") == recomp_before, "not applied while the game runs");

  const auto notices = ApplyPending(scene.import, schema, scene.Logger());
  Check(notices.empty(), "applied without notices");
  Check(Read(scene.container / "sharedstash.bin") == plan.stashes[0].converted, "replaced");
  Check(Read(scene.import / "sharedstash.bin.recomp-bkp") == recomp_before, "old one kept");
  Check(fs::exists(scene.import / "SHAREDSTASH.BIN.bkp"), "PC stash renamed .bkp");
  Check(!fs::exists(scene.import / kPendingFolder), "nothing pending any more");
}

void TestStashChangedBeforeStart(const Schema& schema) {
  Scene scene;
  scene.Copy("pak_pc.zip", scene.import / "Pak.zip");
  scene.Copy("pc_stash.bin", scene.import / "sharedstash.bin");
  ImportFolder folder = ScanImportFolder(scene.import);
  ImportPlan plan = BuildImportPlan(schema, folder, FixturePath("pak_360.zip"), scene.View());
  Check(plan.stashes.size() == 1 && !plan.stashes[0].needs_replace_confirmation(), "empty recomp stash");
  std::string error;
  Check(ConfirmImport(plan, folder, scene.container, {}, scene.Logger(), error), "confirm: " + error);

  // The player puts an item in the recomp stash before restarting.
  scene.Copy("pc_stash.expected.bin", scene.container / "sharedstash.bin");
  const auto notices = ApplyPending(scene.import, schema, scene.Logger());
  Check(notices.size() == 1 && notices[0].kind == Notice::Kind::kStashChanged && notices[0].now == 1 &&
            notices[0].then == 0,
        "not applied, with the reason");
  Check(Read(scene.container / "sharedstash.bin") == Read(FixturePath("pc_stash.expected.bin")), "untouched");

  // The menu asks again, explaining it.
  folder = ScanImportFolder(scene.import);
  plan = BuildImportPlan(schema, folder, FixturePath("pak_360.zip"), scene.View());
  Check(plan.stashes.size() == 1 && plan.stashes[0].needs_replace_confirmation(), "asked again");
  Check(plan.notices.size() == 1 && plan.notices[0].kind == Notice::Kind::kStashChanged, "with the notice");
  Check(ConfirmImport(plan, folder, scene.container, {}, scene.Logger(), error), "confirm: " + error);
  Check(ScanImportFolder(scene.import).notices.empty(), "the notice is shown once");
}

void TestSettings(const Schema& schema) {
  Scene scene;
  scene.Copy("pak_pc.zip", scene.import / "Pak.zip");
  scene.Copy("settings_pc.settings.txt", scene.import / "SETTINGS.TXT");
  scene.Copy("settings_pc.local_settings.txt", scene.import / "local_settings.txt");
  ImportFolder folder = ScanImportFolder(scene.import);
  ImportPlan plan = BuildImportPlan(schema, folder, FixturePath("pak_360.zip"), scene.View());
  Check(plan.settings && plan.settings->ok() && plan.settings->HasChanges(), "settings to import");
  std::string error;
  Check(ConfirmImport(plan, folder, scene.container, {}, scene.Logger(), error), "confirm: " + error);
  Check(ScanImportFolder(scene.import).settings.empty(), "not asked while pending");
  Check(!fs::exists(scene.container / "settings.txt"), "not applied while the game runs");

  const auto notices = ApplyPending(scene.import, schema, scene.Logger());
  Check(notices.empty(), "applied");
  Check(Read(scene.container / "settings.txt") == Read(FixturePath("settings_fresh.settings.txt")) &&
            Read(scene.container / "local_settings.txt") == Read(FixturePath("settings_fresh.local_settings.txt")),
        "created like the Python tool");
  Check(fs::exists(scene.import / "SETTINGS.TXT.bkp") && fs::exists(scene.import / "local_settings.txt.bkp"),
        "PC settings renamed .bkp");

  // Settings that change nothing are not asked about.
  Scene same;
  same.Copy("pak_pc.zip", same.import / "Pak.zip");
  same.Copy("settings_pc.settings.txt", same.import / "settings.txt");
  same.Copy("settings_fresh.settings.txt", same.container / "settings.txt");
  same.Copy("settings_forced.local_settings.txt", same.container / "local_settings.txt");
  folder = ScanImportFolder(same.import);
  plan = BuildImportPlan(schema, folder, FixturePath("pak_360.zip"), same.View());
  Check(!plan.settings, "nothing to change, nothing to ask");
}

void TestLog() {
  Scene scene;
  AppendImportLog(scene.import, "0.SVT: imported as 2.TSV");
  std::ifstream file(scene.import / kLogFile);
  const std::string text(std::istreambuf_iterator<char>(file), {});
  Check(text.size() > 30 && text.find(" UTC  0.SVT: imported as 2.TSV\n") != std::string::npos,
        "log line with a UTC time: " + text);
}

void TestContainerGone(const Schema& schema) {
  Scene scene;
  scene.Copy("pak_pc.zip", scene.import / "Pak.zip");
  scene.Copy("pc_stash.bin", scene.import / "sharedstash.bin");
  ImportFolder folder = ScanImportFolder(scene.import);
  ImportPlan plan = BuildImportPlan(schema, folder, FixturePath("pak_360.zip"), scene.View());
  std::string error;
  Check(ConfirmImport(plan, folder, scene.container, {}, scene.Logger(), error), "confirm: " + error);
  fs::remove_all(scene.container);
  const auto notices = ApplyPending(scene.import, schema, scene.Logger());
  Check(notices.size() == 1 && notices[0].kind == Notice::Kind::kContainerMissing, "container gone");
  Check(ScanImportFolder(scene.import).stashes.size() == 1, "asked again");
}

}  // namespace

int main() {
  auto schema = TheSchema();
  TestFreeNumbers();
  TestScan();
  TestMissingPak(*schema);
  TestCharacters(*schema);
  TestRejected(*schema);
  TestItemOfMissingUnit(*schema);
  TestClassMissing(*schema);
  TestStashReplaced(*schema);
  TestStashChangedBeforeStart(*schema);
  TestSettings(*schema);
  TestContainerGone(*schema);
  TestLog();
  std::puts("save_import import_plan: ok");
  return 0;
}
