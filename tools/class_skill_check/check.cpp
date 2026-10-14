#include "check.h"

#include <map>

#include "game_menu/skill_tree_model.h"
#include "mods/class_list.h"
#include "mods/dat_text.h"

namespace torchlight::tools::class_skills {

namespace st = game_menu::skill_tree;

namespace {

void CollectSkills(const mods::DatBlock& block, std::vector<TreeSkill>& out) {
  for (const mods::DatBlock& child : block.children) {
    if (mods::ClassKey(child.name) == "SKILL") {
      TreeSkill skill;
      if (const mods::DatValue* name = child.Find("NAME")) skill.name = name->value;
      auto number = [&](const char* key) {
        const mods::DatValue* value = child.Find(key);
        if (!value) return -1;
        try {
          return std::stoi(value->value);
        } catch (...) {
          return -1;
        }
      };
      skill.pane = number("PANE");
      skill.row = number("ROW");
      skill.column = number("COLUMN");
      out.push_back(skill);
    } else {
      CollectSkills(child, out);
    }
  }
}

bool InTree(const TreeSkill& s) {
  return s.pane >= 0 && s.pane < st::kPanes && s.row >= 0 && s.row < st::kRows && s.column >= 0 &&
         s.column < st::kColumns;
}

}  // namespace

std::vector<TreeSkill> ReadTreeSkills(const std::vector<uint8_t>& unit_dat) {
  std::vector<TreeSkill> skills;
  const auto blocks = mods::ParseDatText(unit_dat, nullptr);
  if (!blocks) return skills;
  mods::DatBlock root;
  root.children = *blocks;
  CollectSkills(root, skills);
  return skills;
}

std::optional<std::string> GameReadableSkillName(const std::vector<uint8_t>& skill_dat) {
  const bool le = skill_dat.size() >= 2 && skill_dat[0] == 0xFF && skill_dat[1] == 0xFE;
  const bool be = skill_dat.size() >= 2 && skill_dat[0] == 0xFE && skill_dat[1] == 0xFF;
  if (!le && !be) return std::nullopt;
  std::vector<uint8_t> bytes = skill_dat;
  if (be) {
    // ParseDatText reads UTF-16LE: turn the units around.
    for (size_t i = 0; i + 1 < bytes.size(); i += 2) std::swap(bytes[i], bytes[i + 1]);
  }
  return AnySkillName(bytes);
}

std::optional<std::string> AnySkillName(const std::vector<uint8_t>& skill_dat) {
  auto name = mods::FindDatValueAnywhere(skill_dat, "NAME");
  if (!name || name->empty()) return std::nullopt;
  return name;
}

bool IsFailure(Loss why) { return why == Loss::kUnreadable || why == Loss::kNotShown; }

std::string_view LossText(Loss why) {
  switch (why) {
    case Loss::kNotPlaced: return "placed nowhere by its .dat";
    case Loss::kNoDefinition: return "no file defines it";
    case Loss::kCellTaken: return "a later skill of the .dat takes its cell";
    case Loss::kUnreadable: return "its definition is a file the game cannot read";
    case Loss::kNotShown: return "NOT SHOWN by the tree";
  }
  return "";
}

std::vector<LostSkill> LostSkills(const std::vector<TreeSkill>& skills, const std::set<std::string>& readable,
                                  const std::set<std::string>& defined) {
  std::vector<LostSkill> lost;
  // The character's skill list holds the readable ones, in the file's order.
  std::vector<const TreeSkill*> held;
  for (const TreeSkill& s : skills) {
    const std::string key = mods::ClassKey(s.name);
    if (readable.contains(key)) {
      held.push_back(&s);
    } else {
      lost.push_back({s, defined.contains(key) ? Loss::kUnreadable : Loss::kNoDefinition});
    }
  }
  std::vector<st::Skill> list;
  for (size_t i = 0; i < held.size(); ++i) {
    list.push_back({held[i]->pane, held[i]->row, held[i]->column, uint64_t(i)});
  }
  // What the tree shows: the last skill of each cell, as the tree's model builds it.
  const st::Table table = st::BuildTable(list);
  std::map<int, uint64_t> by_slot;
  for (const auto& [slot, id] : table.slots) by_slot[slot] = id;
  std::set<uint64_t> shown;
  for (const auto& [slot, id] : by_slot) shown.insert(id);
  for (const auto& [cell, id] : table.third) shown.insert(id);
  for (size_t i = 0; i < held.size(); ++i) {
    const TreeSkill& s = *held[i];
    if (!InTree(s)) {
      lost.push_back({s, Loss::kNotPlaced});
      continue;
    }
    if (shown.contains(uint64_t(i))) continue;
    bool taken = false;
    for (size_t j = i + 1; j < held.size(); ++j) {
      taken = taken || (held[j]->pane == s.pane && held[j]->row == s.row && held[j]->column == s.column);
    }
    lost.push_back({s, taken ? Loss::kCellTaken : Loss::kNotShown});
  }
  return lost;
}

const KnownLoss* FindKnownLoss(std::string_view class_name, std::string_view skill) {
  for (const KnownLoss& known : kKnownLosses) {
    if (mods::ClassKey(known.class_name) == mods::ClassKey(class_name) &&
        mods::ClassKey(known.skill) == mods::ClassKey(skill)) {
      return &known;
    }
  }
  return nullptr;
}

}  // namespace torchlight::tools::class_skills
