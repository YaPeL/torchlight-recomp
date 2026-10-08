// What a save may reference, read from a game pak at run time (nothing derived from the game is in
// the repository), the checks of a parsed save against it, and the adaptation of the quest dialog
// state to the 360 quest definitions. The port of tools/save_convert/gamedata.py; the messages are
// the same, for the user.

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "save_import/save_tree.h"

namespace torchlight::save_import {

// Sections of a quest's DIALOG block, in the order of the quest's byte_pairs lists in a save
// (quest +204, +220, +236, +252; sub_823C7090 builds them, sub_823CB2B0 reads one state pair per
// line and does not skip extra ones).
inline constexpr std::array<const char*, 4> kDialogSections = {"INTRO", "RETURN", "COMPLETE",
                                                               "PASSIVE"};
// A dialog line: speaker (UNITNAME, upper case) and text.
using DialogLine = std::pair<std::string, std::string>;
using QuestDialogs = std::array<std::vector<DialogLine>, 4>;

struct GameData {
  std::set<int64_t> unit_guids;
  std::set<int64_t> unique_guids;
  std::map<std::string, int64_t> quests;       // upper-case quest NAME -> QUEST_GUID
  std::map<int64_t, std::string> quest_guids;  // the reverse
  std::map<std::string, QuestDialogs> quest_dialogs;
  std::set<std::string> effect_names, skill_names, affix_names;  // upper case

  // Reads every .adm under media/units, media/skills, media/affixes and media/quests. Null with
  // `error` set (for the user) if the pak cannot be read or holds no Torchlight data.
  static std::unique_ptr<GameData> FromPak(const std::filesystem::path& path, SaveError& error);
};

struct Problem {
  std::string kind;     // "unit (UNIT_GUID)", "effect"...
  std::string value;    // as the Python tool prints it: a number, or the text in quotes
  std::string path;
  std::string context;
  std::string Text() const;
};

// An item or a unit (pet, creature) whose unit GUID the 360 data lacks: not a reason to refuse the
// save, since at every start the recomp takes such entries out of the saves, with a backup and a
// notice (mods/save_units.h; a PC mod's items, docs/mods.md section 7f). The character's own class
// and any other missing reference still are.
bool IsRemovableUnit(const Problem& problem);

// Checks every GUID and name of a parsed PC save (or stash) against the 360 data (`target`).
// Quest GUIDs missing there are mapped by quest name through the PC data (`source`, may be null):
// `replacements` (offset in the PC body -> new GUID). Problems mean "do not convert".
void CheckReferences(const Parsed& parsed, const GameData& target, const GameData* source,
                     std::map<size_t, int64_t>& replacements, std::vector<Problem>& problems);
// Writes the replacements into the parsed tree.
void ApplyReplacements(Parsed& parsed, const std::map<size_t, int64_t>& replacements);

// Fits the dialog state of every active quest to the 360 definitions: each 360 line takes the
// state of the PC line with the same speaker and text, a 360 line without one starts as new (0, 0).
// `problems` lists states that cannot be placed without guessing (do not convert).
void AdaptQuestDialogs(Parsed& parsed, const GameData& target, const GameData& source,
                       std::vector<std::string>& changes, std::vector<std::string>& problems);

}  // namespace torchlight::save_import
