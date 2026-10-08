#include "mods/mod_files.h"

#include <system_error>

namespace torchlight::mods {

namespace fs = std::filesystem;

std::u16string NormalizeModPath(std::u16string_view path) {
  std::u16string out;
  out.reserve(path.size());
  for (char16_t c : path) {
    if (c == u'\\') c = u'/';
    else if (c >= u'a' && c <= u'z') c = static_cast<char16_t>(c - u'a' + u'A');
    out.push_back(c);
  }
  while (out.size() >= 2 && out[0] == u'.' && out[1] == u'/') out.erase(0, 2);
  while (!out.empty() && out[0] == u'/') out.erase(0, 1);
  return out;
}

ModFileTable ModFileTable::Scan(const fs::path& mods_folder, const ModPlan& plan) {
  ModFileTable table;
  for (size_t m = 0; m < plan.mods.size(); ++m) {
    const PlannedMod& mod = plan.mods[m];
    if (mod.priority < 0) continue;  // disabled: the game skips it
    const fs::path root = mods_folder / mod.folder;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(root, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
      std::error_code type_ec;
      if (!it->is_regular_file(type_ec)) continue;
      const fs::path relative = fs::relative(it->path(), root, type_ec);
      if (type_ec) continue;
      // As the game's listing spells it: the names on disk, '/' between folders.
      table.Add(m, relative.generic_u16string());
    }
  }
  return table;
}

void ModFileTable::Add(size_t mod, std::u16string_view path_on_disk) {
  files_[NormalizeModPath(path_on_disk)].emplace_back(mod, std::u16string(path_on_disk));
}

std::optional<std::u16string> ModFileTable::Spelling(std::u16string_view request) const {
  const auto it = files_.find(NormalizeModPath(request));
  if (it == files_.end() || it->second.empty()) return std::nullopt;
  size_t first = 0;
  for (size_t i = 1; i < it->second.size(); ++i) {
    if (it->second[i].first < it->second[first].first) first = i;
  }
  return it->second[first].second;
}

}  // namespace torchlight::mods
