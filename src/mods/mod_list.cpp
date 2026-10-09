#include "mods/mod_list.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <iterator>
#include <system_error>

namespace torchlight::mods {

namespace {

std::string Upper(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return s;
}

bool EqualNoCase(const std::string& a, const std::string& b) { return Upper(a) == Upper(b); }

std::optional<std::vector<uint8_t>> ReadFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
}

uint64_t Fnv1a(const std::vector<uint8_t>& bytes) {
  uint64_t h = 0xCBF29CE484222325ull;
  for (uint8_t b : bytes) {
    h ^= b;
    h *= 0x100000001B3ull;
  }
  return h;
}

// The folder name a mods.dat DIRECTORY refers to: its last path component.
std::string FolderOf(const std::string& directory) {
  std::string d = directory;
  while (!d.empty() && (d.back() == '/' || d.back() == '\\')) d.pop_back();
  const size_t slash = d.find_last_of("/\\");
  return slash == std::string::npos ? d : d.substr(slash + 1);
}

bool ParseBool(const std::string& value) {
  const std::string v = Upper(value);
  return v == "1" || v == "TRUE";
}

void CollectEntries(const DatBlock& block, ModList& list) {
  if (const DatValue* dir = block.Find("DIRECTORY")) {
    ModListEntry entry;
    entry.directory = dir->value;
    if (const DatValue* p = block.Find("PRIORITY")) {
      int32_t value = 0;
      const auto* first = p->value.data();
      if (std::from_chars(first, first + p->value.size(), value).ec == std::errc()) entry.priority = value;
    }
    if (const DatValue* c = block.Find("COMPRESSED")) entry.compressed = ParseBool(c->value);
    list.entries.push_back(entry);
  }
  for (const DatBlock& child : block.children) CollectEntries(child, list);
}

}  // namespace

ScanResult ScanModsFolder(const std::filesystem::path& mods_folder) {
  ScanResult result;
  std::error_code ec;
  if (!std::filesystem::is_directory(mods_folder, ec)) return result;
  std::vector<std::filesystem::path> folders;
  for (const auto& entry : std::filesystem::directory_iterator(mods_folder, ec)) {
    if (entry.is_directory(ec)) folders.push_back(entry.path());
  }
  std::sort(folders.begin(), folders.end(), [](const auto& a, const auto& b) {
    return Upper(a.filename().string()) < Upper(b.filename().string());
  });
  for (const auto& folder : folders) {
    const std::string name = folder.filename().string();
    std::filesystem::path descriptor;
    for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
      if (entry.is_regular_file(ec) && EqualNoCase(entry.path().filename().string(), kDescriptorFile)) {
        descriptor = entry.path();
      }
    }
    if (descriptor.empty()) {
      ModFolder mod;
      mod.folder = name;
      result.mods.push_back(std::move(mod));
      continue;
    }
    const auto bytes = ReadFile(descriptor);
    std::string error;
    const auto blocks = bytes ? ParseDatText(*bytes, &error) : std::nullopt;
    if (!blocks) {
      result.skipped.push_back(name + ": " + kDescriptorFile + " unreadable" + (error.empty() ? "" : " (" + error + ")"));
      continue;
    }
    ModFolder mod;
    mod.folder = name;
    mod.descriptor_digest = Fnv1a(*bytes);
    for (const DatBlock& block : *blocks) {
      if (!EqualNoCase(block.name, "MOD")) continue;
      if (const DatValue* v = block.Find("NAME")) mod.name = v->value;
      if (const DatValue* v = block.Find("AUTHOR")) mod.author = v->value;
      if (const DatValue* v = block.Find("DESCRIPTION")) mod.description = v->value;
    }
    result.mods.push_back(std::move(mod));
  }
  return result;
}

std::optional<ModList> ParseModList(const std::vector<uint8_t>& bytes, std::string* error) {
  const auto blocks = ParseDatText(bytes, error);
  if (!blocks) return std::nullopt;
  ModList list;
  for (const DatBlock& block : *blocks) {
    if (const DatValue* c = block.Find("CHECKFORNEW")) list.check_for_new = ParseBool(c->value);
    CollectEntries(block, list);
  }
  return list;
}

std::vector<uint8_t> WriteModList(const ModList& list) {
  DatBlock top{"MODS", {{"BOOL", "CHECKFORNEW", list.check_for_new ? "true" : "false"}}, {}};
  for (const ModListEntry& entry : list.entries) {
    top.children.push_back(DatBlock{"MOD",
                                    {{"STRING", "DIRECTORY", entry.directory},
                                     {"INTEGER", "PRIORITY", std::to_string(entry.priority)},
                                     {"BOOL", "COMPRESSED", entry.compressed ? "true" : "false"}},
                                    {}});
  }
  return WriteDatText({top});
}

ModPlan PlanMods(const ScanResult& scan, const std::optional<ModList>& list) {
  ModPlan plan;
  plan.list.check_for_new = list ? list->check_for_new : true;
  plan.list_changed = !list;
  auto present = [&scan](const std::string& folder) {
    return std::any_of(scan.mods.begin(), scan.mods.end(),
                       [&](const ModFolder& m) { return EqualNoCase(m.folder, folder); });
  };
  int32_t highest = 0;
  if (list) {
    for (const ModListEntry& entry : list->entries) {
      const std::string folder = FolderOf(entry.directory);
      const bool duplicate = std::any_of(plan.list.entries.begin(), plan.list.entries.end(),
                                         [&](const ModListEntry& e) { return EqualNoCase(FolderOf(e.directory), folder); });
      if (folder.empty() || !present(folder) || duplicate) {
        plan.list_changed = true;
        continue;
      }
      plan.list.entries.push_back(entry);
      highest = std::max(highest, entry.priority);
    }
  }
  if (plan.list.check_for_new) {
    for (const ModFolder& mod : scan.mods) {
      const bool listed = std::any_of(plan.list.entries.begin(), plan.list.entries.end(),
                                      [&](const ModListEntry& e) { return EqualNoCase(FolderOf(e.directory), mod.folder); });
      if (listed) continue;
      plan.list.entries.push_back(ModListEntry{mod.folder, ++highest, false});
      plan.list_changed = true;
    }
  }
  for (const ModListEntry& entry : plan.list.entries) {
    plan.mods.push_back(PlannedMod{FolderOf(entry.directory), entry.priority});
  }
  std::stable_sort(plan.mods.begin(), plan.mods.end(), [](const PlannedMod& a, const PlannedMod& b) {
    const bool a_on = a.priority >= 0, b_on = b.priority >= 0;
    if (a_on != b_on) return a_on;
    return a_on && a.priority < b.priority;
  });
  return plan;
}

std::string ModSetFingerprint(const ModPlan& plan, const ScanResult& scan) {
  std::string out;
  for (const PlannedMod& planned : plan.mods) {
    uint64_t digest = 0;
    for (const ModFolder& mod : scan.mods) {
      if (EqualNoCase(mod.folder, planned.folder)) digest = mod.descriptor_digest;
    }
    char hex[17];
    for (int i = 0; i < 16; ++i) hex[i] = "0123456789abcdef"[(digest >> (60 - 4 * i)) & 0xF];
    hex[16] = '\0';
    out += planned.folder + "|" + std::to_string(planned.priority) + "|" + hex + "\n";
  }
  return out;
}

}  // namespace torchlight::mods
