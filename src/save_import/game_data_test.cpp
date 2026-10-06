// Tests for reading the paks, the reference checks and the quest dialog adaptation, against what
// the Python tool gives for the same synthetic paks and save (tests/save_import/fixtures, from
// tests/save_convert/make_fixtures.py).

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "save_import/game_data.h"
#include "save_import/pak.h"
#include "save_import/save_tree.h"

namespace {

using namespace torchlight::save_import;

void Check(bool ok, const std::string& what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    std::exit(1);
  }
}

std::filesystem::path FixturePath(const char* name) {
  return std::filesystem::path(SAVE_IMPORT_FIXTURES) / name;
}

std::string FixtureText(const char* name) {
  std::ifstream file(FixturePath(name), std::ios::binary);
  Check(bool(file), std::string("fixture ") + name);
  return std::string(std::istreambuf_iterator<char>(file), {});
}

Bytes Fixture(const char* name) {
  const std::string text = FixtureText(name);
  return Bytes(text.begin(), text.end());
}

// The same canonical text as make_fixtures.dump().
std::string Dump(const GameData& data) {
  std::vector<std::string> lines;
  for (int64_t g : data.unit_guids) lines.push_back("unit " + std::to_string(g));
  for (int64_t g : data.unique_guids) lines.push_back("unique " + std::to_string(g));
  for (const auto& [name, guid] : data.quests) lines.push_back("quest " + name + " " + std::to_string(guid));
  for (const auto& [name, sections] : data.quest_dialogs) {
    for (size_t i = 0; i < sections.size(); ++i) {
      for (const auto& [unit, text] : sections[i]) {
        lines.push_back("dialog " + name + " " + kDialogSections[i] + " " + unit + "|" + text);
      }
    }
  }
  for (const auto& n : data.effect_names) lines.push_back("effect " + n);
  for (const auto& n : data.skill_names) lines.push_back("skill " + n);
  for (const auto& n : data.affix_names) lines.push_back("affix " + n);
  std::sort(lines.begin(), lines.end());
  std::string out;
  for (const auto& line : lines) out += line + "\n";
  return out;
}

std::unique_ptr<GameData> Load(const char* pak) {
  SaveError error;
  auto data = GameData::FromPak(FixturePath(pak), error);
  Check(data != nullptr, std::string("reads ") + pak + ": " + error.message);
  return data;
}

void TestPaks() {
  Check(Dump(*Load("pak_pc.zip")) == FixtureText("pak_pc.dump.txt"), "PC pak as Python reads it");
  Check(Dump(*Load("pak_360.zip")) == FixtureText("pak_360.dump.txt"), "360 pak as Python reads it");

  SaveError error;
  Check(!GameData::FromPak(FixturePath("pc_save.svt"), error) &&
            error.message.find("could not open") != std::string::npos,
        "a file that is not a zip");

  const std::string text = "Bjørn ¿Sí? Größe 日本 \U0001F600";
  Check(Utf8(Utf16(text)) == text, "UTF-8 <-> UTF-16 round trip");
  Check(Utf16("\xff").size() == 1 && Utf16("\xff")[0] == 0xFFFD, "invalid byte");

  AdmNode root;
  std::string why;
  Check(!ParseAdm({1, 0, 0, 0, 5, 0}, root, why) && why == "truncated adm", "damaged adm");
}

void TestConversion(const Schema& schema) {
  auto pc_data = Load("pak_pc.zip");
  auto x360_data = Load("pak_360.zip");
  SaveError error;
  auto parsed = ReadPc(schema, Fixture("quest_save.svt"), error);
  Check(parsed != nullptr, "reads the save: " + error.message);

  std::map<size_t, int64_t> replacements;
  std::vector<Problem> problems;
  CheckReferences(*parsed, *x360_data, pc_data.get(), replacements, problems);
  Check(problems.empty(), "no reference problems");
  std::vector<std::string> changes, dialog_problems;
  ApplyReplacements(*parsed, replacements);
  AdaptQuestDialogs(*parsed, *x360_data, *pc_data, changes, dialog_problems);

  std::string report;
  for (const auto& [offset, guid] : replacements) {
    report += "replace " + std::to_string(offset) + " " + std::to_string(guid) + "\n";
  }
  for (const auto& c : changes) report += "change " + c + "\n";
  for (const auto& p : dialog_problems) report += "problem " + p + "\n";
  Check(report == FixtureText("quest_save.report.txt"), "same replacements and changes as Python");
  Check(Write360(schema, *parsed) == Fixture("quest_save.expected.tsv"), "same converted save as Python");

  // Without the PC data the changed quest GUID is a problem, not a guess.
  auto again = ReadPc(schema, Fixture("quest_save.svt"), error);
  replacements.clear();
  problems.clear();
  CheckReferences(*again, *x360_data, nullptr, replacements, problems);
  Check(replacements.empty() && !problems.empty() &&
            problems[0].Text().find("the PC Pak.zip is needed") != std::string::npos,
        "changed quest GUID without the PC pak");
}

void TestMissingUnit(const Schema& schema) {
  auto pc_data = Load("pak_pc.zip");
  auto missing = Load("pak_360_missing_unit.zip");
  SaveError error;
  auto parsed = ReadPc(schema, Fixture("pc_save.svt"), error);
  std::map<size_t, int64_t> replacements;
  std::vector<Problem> problems;
  CheckReferences(*parsed, *missing, pc_data.get(), replacements, problems);
  std::string text;
  for (const auto& p : problems) text += p.Text() + "\n";
  Check(text == FixtureText("pc_save.missing_unit.txt"), "same problems as Python: " + text);
}

}  // namespace

int main() {
  std::string error;
  auto schema = Schema::Embedded(error);
  Check(schema != nullptr, "embedded schema: " + error);
  TestPaks();
  TestConversion(*schema);
  TestMissingUnit(*schema);
  std::puts("save_import game_data: ok");
  return 0;
}
