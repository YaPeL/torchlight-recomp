// Tests for the shared stash checks and the settings import, against what the Python tool does
// with the same synthetic files (tests/save_import/fixtures, from tests/save_convert/make_fixtures.py).

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "save_import/settings_file.h"
#include "save_import/stash.h"

namespace {

using namespace torchlight::save_import;

void Check(bool ok, const std::string& what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    std::exit(1);
  }
}

std::string FixtureText(const std::string& name) {
  std::ifstream file(std::filesystem::path(SAVE_IMPORT_FIXTURES) / name, std::ios::binary);
  Check(bool(file), "fixture " + name);
  return std::string(std::istreambuf_iterator<char>(file), {});
}

Bytes Fixture(const std::string& name) {
  const std::string text = FixtureText(name);
  return Bytes(text.begin(), text.end());
}

SettingsFile Settings(const std::string& fixture) {
  SettingsFile file;
  std::string error;
  Check(SettingsFile::Parse(Fixture(fixture), fixture, file, error), "parses " + fixture + ": " + error);
  return file;
}

void TestStash(const Schema& schema) {
  SaveError error;
  auto duplicate = ReadPcStash(schema, Fixture("stash_duplicate.bin"), error);
  Check(duplicate != nullptr, "reads the stash: " + error.message);
  std::string text;
  for (const auto& p : StashSlotProblems(*duplicate)) text += p + "\n";
  Check(text == FixtureText("stash_duplicate.problems.txt"), "same slot problems as Python: " + text);

  auto clean = ReadPcStash(schema, Fixture("pc_stash.bin"), error);
  Check(StashSlotProblems(*clean).empty(), "no slot problems in a clean stash");
  Check(StashItemCount(schema, Fixture("pc_stash.expected.bin")) == 1, "360 stash item count");
}

void TestSettingsFormat() {
  for (const char* name : {"settings_pc.settings.txt", "settings_pc.local_settings.txt",
                           "settings_recomp.settings.txt", "settings_recomp.local_settings.txt"}) {
    Check(Settings(name).ToBytes() == Fixture(name), std::string("round trip of ") + name);
  }
  Check(!Settings("settings_pc.settings.txt").big_endian(), "PC files are little-endian");
  Check(Settings("settings_recomp.settings.txt").big_endian(), "360 files are big-endian");
  Check(Settings("settings_recomp.local_settings.txt").Get("ZIP") == std::string("game:\\pak.zip"),
        "value with ':'");
  Check(Settings("settings_pc.settings.txt").Get("SHOW TIPS") == std::string("0"), "CRLF value");

  SettingsFile out;
  std::string error;
  Check(!SettingsFile::Parse(Bytes{'A', 0}, "x", out, error) && error.find("FF FE") != std::string::npos,
        "no mark");
  Check(!SettingsFile::Parse(Bytes{0xFF, 0xFE, 0, 1, 2, 3}, "x", out, error) &&
            error.find("byte order") != std::string::npos,
        "not text");
}

std::string Optional(const std::optional<std::string>& value) { return value ? *value : "None"; }

void TestSettingsPlans() {
  const std::vector<SettingsFile> pc = {Settings("settings_pc.settings.txt"),
                                        Settings("settings_pc.local_settings.txt")};
  struct Case { const char* name; bool existing, force; };
  for (const Case& c : {Case{"fresh", false, false}, Case{"existing", true, false},
                        Case{"forced", true, true}}) {
    std::string plan_text;
    for (const char* file : kSettingsFiles) {
      std::optional<SettingsFile> recomp;
      if (c.existing) recomp = Settings(std::string("settings_recomp.") + file);
      const SettingsPlan plan = PlanSettingsImport(file, pc, recomp ? &*recomp : nullptr, c.force);
      for (const auto& ch : plan.changes) {
        plan_text += std::string(file) + " change " + ch.key + " " + Optional(ch.old_value) + " " + ch.new_value + "\n";
      }
      for (const auto& ch : plan.skipped) {
        plan_text += std::string(file) + " skip " + ch.key + " " + Optional(ch.old_value) + " " + ch.new_value + "\n";
      }
      for (const auto& p : plan.problems) plan_text += std::string(file) + " problem " + p + "\n";
      SettingsFile result = recomp ? *recomp : SettingsFile::New360();
      for (const auto& ch : plan.changes) result.Set(ch.key, ch.new_value);
      Check(result.ToBytes() == Fixture(std::string("settings_") + c.name + "." + file),
            std::string("same ") + file + " as Python (" + c.name + ")");
    }
    Check(plan_text == FixtureText(std::string("settings_") + c.name + ".plan.txt"),
          std::string("same plan as Python (") + c.name + "): " + plan_text);
  }

  // Values that are not numbers are problems, not imports.
  SettingsFile bad = SettingsFile::New360();
  bad.Set("MUSIC MUTE", "maybe");
  const SettingsPlan plan = PlanSettingsImport("local_settings.txt", {bad}, nullptr, false);
  Check(plan.changes.empty() && plan.problems.size() == 1 &&
            plan.problems[0] == "MUSIC MUTE in the PC local_settings.txt is not a number: 'maybe'",
        "not a number");
}

}  // namespace

int main() {
  std::string error;
  auto schema = Schema::Embedded(error);
  Check(schema != nullptr, "embedded schema: " + error);
  TestStash(*schema);
  TestSettingsFormat();
  TestSettingsPlans();
  std::puts("save_import stash and settings: ok");
  return 0;
}
