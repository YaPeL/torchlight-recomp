#include "save_import/game_data.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <optional>

#include "save_import/pak.h"

namespace torchlight::save_import {
namespace {

// Folders whose definitions a character save can reference.
constexpr std::array<std::string_view, 4> kFolders = {"media/units/", "media/skills/",
                                                      "media/affixes/", "media/quests/"};

bool StartsWith(std::string_view text, std::string_view prefix) {
  return text.substr(0, prefix.size()) == prefix;
}

// A property's value as text, the way the Python tool's str() shows it.
std::string AsText(const AdmNode::Property& property) {
  if (const auto* text = std::get_if<std::string>(&property.value)) return *text;
  if (const auto* number = std::get_if<int64_t>(&property.value)) return std::to_string(*number);
  return "";
}

// The last property called `key` (later ones win, as in a dict built from the list).
const AdmNode::Property* Last(const AdmNode& node, std::string_view key) {
  const AdmNode::Property* found = nullptr;
  for (const auto& property : node.properties) {
    if (property.key == key) found = &property;
  }
  return found;
}

std::optional<int64_t> AsGuid(const AdmNode::Property& property) {
  if (const auto* number = std::get_if<int64_t>(&property.value)) return *number;
  if (const auto* text = std::get_if<std::string>(&property.value)) {
    // A decimal integer, signed or not (the definitions store GUIDs both ways).
    std::string_view digits = *text;
    while (!digits.empty() && digits.front() == ' ') digits.remove_prefix(1);
    while (!digits.empty() && digits.back() == ' ') digits.remove_suffix(1);
    int64_t value = 0;
    auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (ec == std::errc() && end == digits.data() + digits.size()) return value;
    uint64_t unsigned_value = 0;
    auto [uend, uec] =
        std::from_chars(digits.data(), digits.data() + digits.size(), unsigned_value);
    if (uec == std::errc() && uend == digits.data() + digits.size()) {
      return static_cast<int64_t>(unsigned_value);
    }
  }
  return std::nullopt;
}

const AdmNode* FindNode(const AdmNode& node, std::string_view name) {
  if (node.name == name) return &node;
  for (const auto& child : node.children) {
    if (const AdmNode* found = FindNode(child, name)) return found;
  }
  return nullptr;
}

QuestDialogs DialogLines(const AdmNode& root) {
  QuestDialogs sections;
  const AdmNode* dialog = FindNode(root, "DIALOG");
  if (!dialog) return sections;
  for (size_t i = 0; i < kDialogSections.size(); ++i) {
    for (const auto& child : dialog->children) {
      if (child.name != kDialogSections[i]) continue;
      const auto* unit = Last(child, "UNITNAME");
      const auto* text = Last(child, "DIALOG");
      sections[i].emplace_back(Upper(unit ? AsText(*unit) : ""), text ? AsText(*text) : "");
    }
  }
  return sections;
}

void AddEffects(const AdmNode& node, GameData& data) {
  if (node.name == "EFFECT") {
    for (const auto& property : node.properties) {
      if (property.key == "NAME") {
        const std::string name = AsText(property);
        if (!name.empty()) data.effect_names.insert(Upper(name));
      }
    }
  }
  for (const auto& child : node.children) AddEffects(child, data);
}

void Add(const std::string& path, const AdmNode& root, GameData& data) {
  for (const auto& property : root.properties) {
    if (property.key == "UNIT_GUID") {
      if (auto guid = AsGuid(property)) data.unit_guids.insert(*guid);
    } else if (property.key == "UNIQUE_GUID") {
      if (auto guid = AsGuid(property)) data.unique_guids.insert(*guid);
    }
  }
  const auto* name_property = Last(root, "NAME");
  const std::string name = Upper(name_property ? AsText(*name_property) : "");
  if (StartsWith(path, "media/skills/") && !name.empty()) data.skill_names.insert(name);
  if (StartsWith(path, "media/affixes/") && !name.empty()) data.affix_names.insert(name);
  if (StartsWith(path, "media/quests/") && !name.empty()) {
    if (const auto* quest_guid = Last(root, "QUEST_GUID")) {
      if (auto guid = AsGuid(*quest_guid)) {
        data.quests[name] = *guid;
        data.quest_dialogs[name] = DialogLines(root);
      }
    }
  }
  AddEffects(root, data);
}

std::string KindName(const std::string& role) {
  if (role == "unit_guid") return "unit (UNIT_GUID)";
  if (role == "unique_guid") return "unique (UNIQUE_GUID)";
  if (role == "quest_guid") return "quest (QUEST_GUID)";
  if (role == "effect_name") return "effect";
  if (role == "effect_source") return "effect source (skill, affix or effect)";
  if (role == "quest_name") return "quest";
  return role;
}

bool IsNone(int64_t guid) { return guid == -1 || guid == 0; }

std::string ElementName(const Ref& ref) {
  if (!ref.element) return "";
  for (const char* key : {"s0", "name"}) {
    const Node* value = ref.element->Find(key);
    if (value && value->kind == Node::Kind::kText && !value->text.empty()) return Utf8(value->text);
  }
  return "";
}

bool IsNewLine(const Node& state) {
  return std::all_of(state.items.begin(), state.items.end(),
                     [](const Node& v) { return v.number == 0; });
}

std::string StateText(const Node& state) {
  std::string out = "(";
  bool first = true;
  for (const Node& value : state.items) {
    if (!first) out += ", ";
    out += std::to_string(value.number);
    first = false;
  }
  return out + ")";
}

}  // namespace

std::unique_ptr<GameData> GameData::FromPak(const std::filesystem::path& path, SaveError& error) {
  std::string why;
  auto pak = Pak::Open(path, why);
  const std::string shown = path.string();
  if (!pak) {
    error.message = "could not open " + shown + " as a game pak: " + why;
    return nullptr;
  }
  auto data = std::make_unique<GameData>();
  size_t read = 0;
  bool failed = false;
  pak->ForEach([&](uint32_t index, const std::string& name) {
    if (name.size() < 4 || name.compare(name.size() - 4, 4, ".adm") != 0) return true;
    if (std::none_of(kFolders.begin(), kFolders.end(),
                     [&](std::string_view folder) { return StartsWith(name, folder); })) {
      return true;
    }
    std::vector<uint8_t> bytes;
    AdmNode root;
    if (!pak->Read(index, bytes, why) || !ParseAdm(bytes, root, why)) {
      error.message = "could not read " + name + " inside " + shown + ": " + why;
      failed = true;
      return false;
    }
    Add(name, root, *data);
    ++read;
    return true;
  });
  if (failed) return nullptr;
  if (!read) {
    error.message = shown + " has no Torchlight data (media/units, media/skills...)";
    return nullptr;
  }
  for (const auto& [name, guid] : data->quests) data->quest_guids[guid] = name;
  return data;
}

std::string Problem::Text() const {
  return kind + " " + value + " at " + path + (context.empty() ? "" : " (" + context + ")");
}

void CheckReferences(const Parsed& parsed, const GameData& target, const GameData* source,
                     std::map<size_t, int64_t>& replacements, std::vector<Problem>& problems) {
  std::set<std::pair<std::string, std::string>> seen;
  std::set<std::string> effect_sources = target.skill_names;
  effect_sources.insert(target.affix_names.begin(), target.affix_names.end());
  effect_sources.insert(target.effect_names.begin(), target.effect_names.end());

  auto problem = [&](const Ref& ref, const std::string& value, const std::string& context) {
    if (!seen.insert({ref.role, value}).second) return;
    problems.push_back({KindName(ref.role), value, ref.path, context});
  };

  for (const Ref& ref : parsed.refs) {
    const std::string element_name = ElementName(ref);
    if (ref.role == "unit_guid" || ref.role == "unique_guid" || ref.role == "quest_guid") {
      const int64_t value = Signed64(ref.value->number);
      if (IsNone(value)) continue;
      if (!ref.only_if_set.empty()) {
        const Node* guard = ref.element ? ref.element->Find(ref.only_if_set) : nullptr;
        if (IsNone(guard ? Signed64(guard->number) : -1)) continue;  // e.g. gold piles
      }
      const std::string shown = std::to_string(value);
      if (ref.role == "unit_guid" && !target.unit_guids.contains(value)) {
        problem(ref, shown, element_name);
      } else if (ref.role == "unique_guid" && !target.unique_guids.contains(value)) {
        problem(ref, shown, element_name);
      } else if (ref.role == "quest_guid" && !target.quest_guids.contains(value)) {
        const std::string* name = nullptr;
        if (source) {
          auto found = source->quest_guids.find(value);
          if (found != source->quest_guids.end()) name = &found->second;
        }
        if (name && target.quests.contains(*name)) {
          replacements[ref.offset] = target.quests.at(*name);
        } else if (!source) {
          problem(ref, shown, element_name + "; the PC Pak.zip is needed to know which quest it is");
        } else {
          problem(ref, shown, element_name);
        }
      }
      continue;
    }
    const std::string original = Utf8(ref.value->text);
    const std::string text = Upper(original);
    if (text.empty()) continue;
    if (ref.role == "effect_name" && !target.effect_names.contains(text)) {
      problem(ref, PythonRepr(original), element_name);
    } else if (ref.role == "effect_source" && !effect_sources.contains(text)) {
      problem(ref, PythonRepr(original), element_name);
    } else if (ref.role == "quest_name" && !target.quests.contains(text)) {
      problem(ref, PythonRepr(original), "");
    }
  }
}

void ApplyReplacements(Parsed& parsed, const std::map<size_t, int64_t>& replacements) {
  for (Ref& ref : parsed.refs) {
    auto found = replacements.find(ref.offset);
    if (found != replacements.end()) ref.value->number = static_cast<uint64_t>(found->second);
  }
}

void AdaptQuestDialogs(Parsed& parsed, const GameData& target, const GameData& source,
                       std::vector<std::string>& changes, std::vector<std::string>& problems) {
  Node* quests = parsed.tree.Find("quests");
  Node* active = quests ? quests->Find("active") : nullptr;
  if (!active) return;
  for (Node& quest : active->items) {
    const Node* name_node = quest.Find("name");
    Node* body = quest.Find("quest");
    Node* pairs = body ? body->Find("byte_pairs") : nullptr;
    if (!name_node || !pairs || pairs->items.size() != kDialogSections.size()) continue;
    const std::string quest_name = Utf8(name_node->text);
    auto new_found = target.quest_dialogs.find(Upper(quest_name));
    if (new_found == target.quest_dialogs.end()) continue;  // unknown to the 360: skipped by offset
    auto old_found = source.quest_dialogs.find(Upper(quest_name));
    const QuestDialogs* old_lines = old_found == source.quest_dialogs.end() ? nullptr : &old_found->second;

    auto section_states = pairs->items.begin();
    for (size_t index = 0; index < kDialogSections.size(); ++index, ++section_states) {
      const std::vector<DialogLine>& fresh = new_found->second[index];
      const std::vector<DialogLine>* old = old_lines ? &(*old_lines)[index] : nullptr;
      Node& states = *section_states;
      const std::string where = "quest " + quest_name + ", " + kDialogSections[index] + " dialog";
      const std::vector<DialogLine>& expected = old ? *old : fresh;
      if (states.items.size() != expected.size()) {
        problems.push_back(where + ": the save has " + std::to_string(states.items.size()) +
                           " lines and the " + (old ? "PC" : "360") + " game data " +
                           std::to_string(expected.size()));
        continue;
      }
      if (!old || *old == fresh) continue;

      std::vector<const Node*> state_list;
      for (const Node& state : states.items) state_list.push_back(&state);
      std::list<Node> adapted;
      std::set<size_t> used, ambiguous;
      auto new_state = [&]() {
        Node pair;
        pair.kind = Node::Kind::kList;
        pair.items.resize(2);
        return pair;
      };
      for (const DialogLine& line : fresh) {
        std::vector<size_t> matches;
        for (size_t i = 0; i < old->size(); ++i) {
          if ((*old)[i] == line) matches.push_back(i);
        }
        if (matches.size() == 1) {
          used.insert(matches[0]);
          adapted.push_back(*state_list[matches[0]]);
        } else if (std::any_of(matches.begin(), matches.end(),
                               [&](size_t i) { return !IsNewLine(*state_list[i]); })) {
          ambiguous.insert(matches.begin(), matches.end());
          problems.push_back(where + ": the line of " + line.first + " appears " +
                             std::to_string(matches.size()) +
                             " times in the PC data and one has a state; which one is meant "
                             "cannot be known");
          adapted.push_back(new_state());
        } else {
          adapted.push_back(new_state());
        }
      }
      for (size_t i = 0; i < state_list.size(); ++i) {
        if (!used.contains(i) && !ambiguous.contains(i) && !IsNewLine(*state_list[i])) {
          problems.push_back(where + ": the line of " + (*old)[i].first + " has state " +
                             StateText(*state_list[i]) +
                             " and the 360 game has no line with the same speaker and text");
        }
      }
      states.items = std::move(adapted);
      changes.push_back(where + ": " + std::to_string(old->size()) + " lines (PC) -> " +
                        std::to_string(fresh.size()) + " (360)");
    }
  }
}

}  // namespace torchlight::save_import
