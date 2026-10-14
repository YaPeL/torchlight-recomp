// Every skill a class's unit definition places in its tree, against what the skill tree shows
// (docs/mods.md 7l): the comparison behind class_skill_check, for the mods' classes and the game's.
//
// A skill of a class's .dat is shown when the game has its definition (the game's pak, or a mod's
// text file the game can read: UTF-16 with its byte order mark, as PC reads them), its cell is in
// the tree (3 panes x 6 rows x 3 columns) and no later skill of the list takes the same cell. The
// cells and the "later skill wins" come from the tree's own model (game_menu/skill_tree_model.h
// BuildTable), so a change there that drops a skill shows here.
//
// Some skills are not shown because of the mod's own data, and PC does the same with them: a skill
// placed nowhere (the game reads a missing PANE, ROW or COLUMN as -1, sub_823ACC60 @0x823ACCA0 and
// @0x823AD66C; or a cell outside the tree), one no file defines, or one a later skill of the same
// .dat replaces in its cell. Those are reported, not failures. A failure is a skill whose definition
// exists but cannot be read (unless it is a known loss), or one defined, placed and alone in its
// cell that the tree does not show. No guest, OGRE or platform types.

#pragma once

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace torchlight::tools::class_skills {

struct TreeSkill {
  std::string name;
  int pane = -1, row = -1, column = -1;
};

// The [SKILL] blocks of a class's text unit definition, in order ("//" lines skipped, as the game
// skips them). Blocks without a PANE, ROW or COLUMN keep -1 there.
std::vector<TreeSkill> ReadTreeSkills(const std::vector<uint8_t>& unit_dat);

// The NAME the game reads from a skill's text definition: the file must be UTF-16 with its byte
// order mark (PC reads every text file as UTF-16, mods/text_encoding.h); none otherwise. The first
// NAME line, as tolerant of a broken line as the game is.
std::optional<std::string> GameReadableSkillName(const std::vector<uint8_t>& skill_dat);
// The NAME a skill's text definition sets, whatever its encoding (ASCII, UTF-8 or UTF-16).
std::optional<std::string> AnySkillName(const std::vector<uint8_t>& skill_dat);

enum class Loss {
  // The mod's own data (as on PC): reported.
  kNotPlaced,     // PANE, ROW or COLUMN missing or outside the tree
  kNoDefinition,  // no file defines it
  kCellTaken,     // a later skill of the same .dat takes its cell
  // Failures, unless known.
  kUnreadable,    // a definition exists, the game cannot read it
  kNotShown,      // defined, readable, placed and alone in its cell, yet not shown
};
bool IsFailure(Loss why);
struct LostSkill {
  TreeSkill skill;
  Loss why;
};
std::string_view LossText(Loss why);

// The skills of `skills` the tree does not show, and why. `readable`: the names (mods::ClassKey) of
// the definitions the game reads; `defined`: those any file defines, readable or not.
std::vector<LostSkill> LostSkills(const std::vector<TreeSkill>& skills, const std::set<std::string>& readable,
                                  const std::set<std::string>& defined);

// Losses known and explained (class and skill compared as mods::ClassKey).
struct KnownLoss {
  std::string_view class_name, skill, reason;
};
inline constexpr KnownLoss kKnownLosses[] = {
    {"Kensai", "RAGEECHO",
     "JCC - Abomination's rageecho.dat is 8-bit text; PC loses it too (docs/mods.md 7l)"},
};
const KnownLoss* FindKnownLoss(std::string_view class_name, std::string_view skill);

}  // namespace torchlight::tools::class_skills
