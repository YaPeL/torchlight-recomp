// class_skill_check: every class's tree skills against what the skill tree shows (check.h), for the
// game's classes and those a mods folder offers at character creation (mods/class_list.h).
//
// Usage: class_skill_check PAK MODS_FOLDER
//   PAK          the game's pak.zip (its skills and its classes' unit definitions)
//   MODS_FOLDER  a mods folder (with or without mods.dat), as the game reads it
// Prints each class with its skills shown and lost; exits 1 when a skill is lost that is not one of
// kKnownLosses. Runs on the user's own data: nothing of the game or the mods is in the repository.

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include "check.h"
#include "mods/class_list.h"
#include "mods/dat_text.h"
#include "mods/mod_list.h"
#include "save_import/pak.h"

namespace fs = std::filesystem;
namespace cs = torchlight::tools::class_skills;
namespace m = torchlight::mods;
namespace si = torchlight::save_import;

namespace {

std::vector<uint8_t> ReadFile(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

bool Under(const fs::path& path, std::string_view folder) {
  std::string text = path.generic_string();
  for (char& c : text) c = char(std::tolower(static_cast<unsigned char>(c)));
  return text.find(folder) != std::string::npos;
}

bool HasExtension(const fs::path& path, std::string_view extension) {
  std::string ext = path.extension().string();
  for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
  return ext == extension;
}

// An .adm's NAME, or none.
std::optional<std::string> AdmName(const std::vector<uint8_t>& bytes) {
  si::AdmNode root;
  std::string error;
  if (!si::ParseAdm(bytes, root, error)) return std::nullopt;
  const si::AdmNode::Property* name = root.Find("NAME");
  if (!name || !std::holds_alternative<std::string>(name->value)) return std::nullopt;
  return std::get<std::string>(name->value);
}

// The game's classes' tree skills, from their compiled unit definitions in the pak.
std::vector<cs::TreeSkill> AdmTreeSkills(const si::AdmNode& unit) {
  std::vector<cs::TreeSkill> skills;
  for (const si::AdmNode& child : unit.children) {
    if (m::ClassKey(child.name) != "SKILL") continue;
    cs::TreeSkill skill;
    if (const auto* name = child.Find("NAME"); name && std::holds_alternative<std::string>(name->value)) {
      skill.name = std::get<std::string>(name->value);
    }
    auto number = [&](const char* key) {
      const auto* value = child.Find(key);
      return value && std::holds_alternative<int64_t>(value->value) ? int(std::get<int64_t>(value->value)) : -1;
    };
    skill.pane = number("PANE");
    skill.row = number("ROW");
    skill.column = number("COLUMN");
    skills.push_back(skill);
  }
  return skills;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::fprintf(stderr, "usage: class_skill_check PAK MODS_FOLDER\n");
    return 2;
  }
  const fs::path pak_path = argv[1], mods_folder = argv[2];
  std::string error;
  auto pak = si::Pak::Open(pak_path, error);
  if (!pak) {
    std::fprintf(stderr, "%s: %s\n", pak_path.string().c_str(), error.c_str());
    return 2;
  }

  // Skill definitions and class units: the pak's, then the enabled mods' in search order (the first
  // mod that has a class's unit wins, as the game's file lookup does).
  std::set<std::string> readable, defined;
  std::map<std::string, std::vector<cs::TreeSkill>> class_skills;  // by ClassKey
  pak->ForEach([&](uint32_t index, const std::string& name) {
    const bool skill = name.starts_with("media/skills/") && name.ends_with(".adm");
    const bool player = name.starts_with("media/units/players/") && name.ends_with(".adm");
    if (!skill && !player) return true;
    std::vector<uint8_t> bytes;
    if (!pak->Read(index, bytes, error)) return true;
    si::AdmNode root;
    if (!si::ParseAdm(bytes, root, error)) return true;
    const auto* unit_name = root.Find("NAME");
    if (!unit_name || !std::holds_alternative<std::string>(unit_name->value)) return true;
    const std::string key = m::ClassKey(std::get<std::string>(unit_name->value));
    if (skill) {
      readable.insert(key);
      defined.insert(key);
    }
    if (player) class_skills[key] = AdmTreeSkills(root);
    return true;
  });

  const m::ScanResult scan = m::ScanModsFolder(mods_folder);
  std::optional<m::ModList> list;
  if (fs::exists(mods_folder / "mods.dat")) {
    list = m::ParseModList(ReadFile(mods_folder / "mods.dat"), nullptr);
  }
  const m::ModPlan plan = m::PlanMods(scan, list);
  std::set<std::string> mod_units;
  for (const m::PlannedMod& mod : plan.mods) {
    if (mod.priority < 0) continue;
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(mods_folder / mod.folder, ec)) {
      if (!entry.is_regular_file(ec)) continue;
      const fs::path& path = entry.path();
      if (Under(path, "/media/skills/")) {
        if (HasExtension(path, ".dat")) {
          const std::vector<uint8_t> bytes = ReadFile(path);
          if (auto name = cs::GameReadableSkillName(bytes)) readable.insert(m::ClassKey(*name));
          if (auto name = cs::AnySkillName(bytes)) defined.insert(m::ClassKey(*name));
        } else if (HasExtension(path, ".adm")) {
          if (auto name = AdmName(ReadFile(path))) {
            readable.insert(m::ClassKey(*name));
            defined.insert(m::ClassKey(*name));
          }
        }
      } else if (Under(path, "/media/units/players/") && HasExtension(path, ".dat")) {
        const std::vector<uint8_t> bytes = ReadFile(path);
        const auto blocks = m::ParseDatText(bytes, nullptr);
        if (!blocks || blocks->empty()) continue;
        const m::DatValue* name = blocks->front().Find("NAME");
        if (!name) continue;
        const std::string key = m::ClassKey(name->value);
        if (mod_units.insert(key).second) class_skills[key] = cs::ReadTreeSkills(bytes);
      }
    }
  }

  // The classes the creation screen offers.
  const std::vector<std::string> classes =
      m::ClassList(m::ModClassButtons(mods_folder, plan), m::ModPlayerUnits(mods_folder, plan));
  int unexplained = 0, known = 0, by_data = 0;
  std::printf("%zu skill definitions (%zu the game reads), %zu classes\n", defined.size(), readable.size(),
              classes.size());
  for (const std::string& class_name : classes) {
    const auto it = class_skills.find(m::ClassKey(class_name));
    if (it == class_skills.end()) {
      std::printf("%s: no unit definition found\n", class_name.c_str());
      ++unexplained;
      continue;
    }
    const auto lost = cs::LostSkills(it->second, readable, defined);
    std::printf("%s: %zu skills, %zu shown\n", class_name.c_str(), it->second.size(),
                it->second.size() - lost.size());
    for (const cs::LostSkill& l : lost) {
      const cs::KnownLoss* k = cs::FindKnownLoss(class_name, l.skill.name);
      const bool failure = cs::IsFailure(l.why) && !k;
      const char* tag = failure ? "FAIL" : k ? "known" : "mod data";
      std::printf("  %s: %s (%d, %d, %d): %s%s%s\n", tag, l.skill.name.c_str(), l.skill.pane, l.skill.row,
                  l.skill.column, std::string(cs::LossText(l.why)).c_str(), k ? "; " : "",
                  k ? std::string(k->reason).c_str() : "");
      if (failure) {
        ++unexplained;
      } else if (k) {
        ++known;
      } else {
        ++by_data;
      }
    }
  }
  std::printf("%d not shown because of the mods' own data (as on PC), %d known loss(es), %d failure(s)\n",
              by_data, known, unexplained);
  return unexplained ? 1 : 0;
}
