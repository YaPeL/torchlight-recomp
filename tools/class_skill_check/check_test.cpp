// Tests for the class skill check (check.h) on synthetic definitions.

#include "check.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace cs = torchlight::tools::class_skills;

namespace {

int failures = 0;
void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

std::vector<uint8_t> Utf16(const std::string& text, bool little = true) {
  std::vector<uint8_t> bytes = little ? std::vector<uint8_t>{0xFF, 0xFE} : std::vector<uint8_t>{0xFE, 0xFF};
  for (char c : text) {
    if (little) {
      bytes.push_back(uint8_t(c));
      bytes.push_back(0);
    } else {
      bytes.push_back(0);
      bytes.push_back(uint8_t(c));
    }
  }
  return bytes;
}

std::string SkillBlock(const std::string& name, int pane, int row, int column, bool commented = false) {
  const std::string c = commented ? "// " : "";
  return c + "[SKILL]\r\n" + c + "<STRING>NAME:" + name + "\r\n" + c + "<INTEGER>PANE:" + std::to_string(pane) +
         "\r\n" + c + "<INTEGER>ROW:" + std::to_string(row) + "\r\n" + c + "<INTEGER>COLUMN:" +
         std::to_string(column) + "\r\n" + c + "[/SKILL]\r\n";
}

}  // namespace

int main() {
  // A class's skills: commented blocks skipped, in the file's order.
  {
    const std::string unit = "[UNIT]\r\n<STRING>NAME:Tester\r\n" + SkillBlock("One", 0, 0, 0) +
                             SkillBlock("Gone", 0, 1, 0, true) + SkillBlock("Two", 1, 2, 2) + "[/UNIT]\r\n";
    const auto skills = cs::ReadTreeSkills(Utf16(unit));
    Check(skills.size() == 2 && skills[0].name == "One" && skills[1].name == "Two" && skills[1].column == 2,
          "tree skills read, the commented one skipped");
  }

  // A skill's definition: read only as UTF-16 with its mark.
  {
    const std::string skill = "[SKILL]\r\n<STRING>NAME:Rage Echo\r\n[/SKILL]\r\n";
    Check(cs::GameReadableSkillName(Utf16(skill)) == "Rage Echo", "UTF-16LE read");
    Check(cs::GameReadableSkillName(Utf16(skill, false)) == "Rage Echo", "UTF-16BE read");
    Check(!cs::GameReadableSkillName(std::vector<uint8_t>(skill.begin(), skill.end())),
          "an 8-bit file is not read, as on PC");
    Check(cs::AnySkillName(std::vector<uint8_t>(skill.begin(), skill.end())) == "Rage Echo",
          "but it defines the skill");
    const std::string broken = "[SKILL]\r\n<STRING>NAME:Rat\r\n<TRANSLATE>DISPLAYNAMERat\r\n[/SKILL]\r\n";
    Check(cs::GameReadableSkillName(Utf16(broken)) == "Rat", "a broken line elsewhere does not lose the NAME");
  }

  // What the tree shows, and why the rest is not.
  {
    const std::set<std::string> readable = {"A", "B", "C", "D", "E", "F", "N"};
    const std::set<std::string> defined = {"A", "B", "C", "D", "E", "F", "N", "U"};
    const std::vector<cs::TreeSkill> skills = {
        {"A", 0, 0, 0},    // shown
        {"B", 1, 1, 2},    // third column: shown (its game slot is (1, 2, 0)'s)
        {"C", 1, 2, 0},    // shown: a third-column skill does not take its cell
        {"D", 2, 6, 0},    // outside the tree
        {"N", -1, 2, 1},   // no PANE: placed nowhere
        {"E", 0, 3, 1},    // taken by F
        {"F", 0, 3, 1},
        {"X", 0, 4, 0},    // no definition
        {"U", 0, 5, 0},    // a definition the game cannot read
    };
    const auto lost = cs::LostSkills(skills, readable, defined);
    Check(lost.size() == 5, "five not shown");
    auto lost_as = [&](const char* name, cs::Loss why) {
      for (const auto& l : lost) {
        if (l.skill.name == name) return l.why == why;
      }
      return false;
    };
    Check(lost_as("X", cs::Loss::kNoDefinition), "a skill no file defines");
    Check(lost_as("U", cs::Loss::kUnreadable), "a definition the game cannot read");
    Check(lost_as("D", cs::Loss::kNotPlaced) && lost_as("N", cs::Loss::kNotPlaced), "placed nowhere");
    Check(lost_as("E", cs::Loss::kCellTaken), "a cell a later skill takes");
    Check(cs::IsFailure(cs::Loss::kUnreadable) && cs::IsFailure(cs::Loss::kNotShown) &&
              !cs::IsFailure(cs::Loss::kNotPlaced) && !cs::IsFailure(cs::Loss::kNoDefinition) &&
              !cs::IsFailure(cs::Loss::kCellTaken),
          "only an unreadable definition or a skill the tree drops fail");
    Check(cs::LostSkills({{"a", 0, 0, 0}}, {"A"}, {"A"}).empty(), "names compared in any case");
    // Every cell of the tree, each with its own skill: all shown.
    std::vector<cs::TreeSkill> full;
    std::set<std::string> names;
    for (int p = 0; p < 3; ++p) {
      for (int r = 0; r < 6; ++r) {
        for (int c = 0; c < 3; ++c) {
          full.push_back({"S" + std::to_string(p) + std::to_string(r) + std::to_string(c), p, r, c});
          names.insert(full.back().name);
        }
      }
    }
    Check(cs::LostSkills(full, names, names).empty(), "all 54 cells shown");
  }

  // Known losses.
  Check(cs::FindKnownLoss("KENSAI", "RageEcho") != nullptr, "the Kensai's Rage Echo is known");
  Check(cs::FindKnownLoss("Kensai", "Ritual") == nullptr, "nothing else is");

  if (failures) {
    std::fprintf(stderr, "%d failure(s)\n", failures);
    return EXIT_FAILURE;
  }
  std::printf("class_skill_check_test: ok\n");
  return EXIT_SUCCESS;
}
