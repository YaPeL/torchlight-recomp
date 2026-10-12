#include "mods/texture_files.h"

#include <atomic>
#include <mutex>
#include <system_error>
#include <unordered_map>

namespace torchlight::mods {

std::string TextureNameKey(std::string_view name) {
  std::string key;
  key.reserve(name.size());
  for (char c : name) {
    if (c == '\\') c = '/';
    if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    if (c == '/' && key.empty()) continue;
    key.push_back(c);
  }
  return key;
}

TextureResolver::Folder TextureResolver::List(const std::filesystem::path& root) {
  Folder folder;
  std::error_code ec;
  for (auto it = std::filesystem::recursive_directory_iterator(root, ec);
       !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
    std::error_code file_ec;
    if (!it->is_regular_file(file_ec)) continue;
    const std::string relative = std::filesystem::relative(it->path(), root, file_ec).generic_string();
    if (file_ec) continue;
    folder.files.emplace(TextureNameKey(relative), it->path());  // the first of names equal but for case
  }
  return folder;
}

std::optional<std::filesystem::path> TextureResolver::Folder::Find(const std::string& key) const {
  auto it = files.find(key);
  if (it == files.end()) return std::nullopt;
  return it->second;
}

TextureResolver::TextureResolver(const TextureLocations& locations) {
  if (!locations.replacements.empty()) replacements_ = List(locations.replacements);
  for (const auto& mod : locations.mods) mods_.emplace_back(mod.group, List(mod.folder));
}

TextureFile TextureResolver::Find(std::string_view name, std::string_view group,
                                  const std::function<bool(std::string_view)>& pak_has) const {
  const std::string key = TextureNameKey(name);
  auto game = [&]() -> std::optional<TextureFile> {
    if (replacements_) {
      if (auto file = replacements_->Find(key)) return TextureFile{TextureSource::kReplacements, *file};
    }
    if (pak_has && pak_has(key)) return TextureFile{TextureSource::kPak, {}};
    return std::nullopt;
  };
  auto mods = [&](std::string_view skip) -> std::optional<TextureFile> {
    for (const auto& [mod_group, folder] : mods_) {
      if (mod_group == skip) continue;
      if (auto file = folder.Find(key)) return TextureFile{TextureSource::kMod, *file};
    }
    return std::nullopt;
  };
  for (const auto& [mod_group, folder] : mods_) {
    if (mod_group != group) continue;
    if (auto file = folder.Find(key)) return {TextureSource::kMod, *file};
    if (auto file = game()) return *file;
    if (auto file = mods(group)) return *file;
    return {};
  }
  if (auto file = game()) return *file;
  if (auto file = mods({})) return *file;
  return {};
}

namespace {
std::mutex g_mutex;
std::shared_ptr<const TextureResolver> g_resolver;
TextureLocations g_locations;
std::unordered_set<std::string> g_pak_names;
std::atomic<bool> g_check{false};
std::unordered_map<std::string, std::pair<std::string, std::string>> g_guest_opens;  // key -> container, path
TextureCheckCounts g_counts;

std::string Lower(std::string_view text) {
  std::string out(text);
  for (char& c : out) c = char(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
  return out;
}
}  // namespace

void SetTextureLocations(const TextureLocations& locations) {
  auto resolver = std::make_shared<const TextureResolver>(locations);
  std::lock_guard lock(g_mutex);
  g_resolver = resolver->empty() ? nullptr : resolver;
  g_locations = locations;
}

void SetTextureCheck(bool on) { g_check = on; }
bool TextureCheckOn() { return g_check; }

void NoteGuestTextureOpen(std::string_view name, std::string_view container, std::string_view path) {
  if (!g_check) return;
  std::lock_guard lock(g_mutex);
  g_guest_opens[TextureNameKey(name)] = {std::string(container), std::string(path)};
}

TextureFile SourceOfGuestContainer(std::string_view container, const TextureLocations& locations) {
  const std::string c = Lower(container);
  if (c.starts_with("tlrepl:")) return {TextureSource::kReplacements, locations.replacements};
  for (const auto& mod : locations.mods) {
    if (!mod.device.empty() && c.starts_with(Lower(mod.device))) return {TextureSource::kMod, mod.folder};
  }
  return {TextureSource::kPak, {}};
}

std::optional<std::string> CheckTextureChoice(std::string_view name, const TextureFile& chosen) {
  const std::string key = TextureNameKey(name);
  std::lock_guard lock(g_mutex);
  auto it = g_guest_opens.find(key);
  if (it == g_guest_opens.end()) {
    ++g_counts.unreported;
    return std::nullopt;
  }
  ++g_counts.compared;
  const TextureFile guest = SourceOfGuestContainer(it->second.first, g_locations);
  // The same source, and for a mod the same mod (the chosen file lies in its folder).
  bool same = guest.source == chosen.source;
  if (same && guest.source == TextureSource::kMod) {
    const std::string folder = guest.path.lexically_normal().generic_string();
    same = chosen.path.lexically_normal().generic_string().starts_with(folder);
  }
  if (same) return std::nullopt;
  ++g_counts.differing;
  auto describe = [](const TextureFile& f) {
    switch (f.source) {
      case TextureSource::kPak: return std::string("the pak");
      case TextureSource::kReplacements: return "the replacements folder (" + f.path.string() + ")";
      case TextureSource::kMod: return "a mod (" + f.path.string() + ")";
      case TextureSource::kNone: return std::string("nothing");
    }
    return std::string("?");
  };
  return std::string(name) + ": the game opened " + it->second.first + it->second.second + " (" + describe(guest) +
         "), the host chose " + describe(chosen);
}

TextureCheckCounts TextureCheckTotals() {
  std::lock_guard lock(g_mutex);
  return g_counts;
}

std::shared_ptr<const TextureResolver> CurrentTextureResolver() {
  std::lock_guard lock(g_mutex);
  return g_resolver;
}

void SetPakNames(const std::vector<std::string>& names) {
  std::unordered_set<std::string> keys;
  keys.reserve(names.size());
  for (const auto& n : names) keys.insert(TextureNameKey(n));
  std::lock_guard lock(g_mutex);
  g_pak_names = std::move(keys);
}

bool PakHas(std::string_view name) {
  const std::string key = TextureNameKey(name);
  std::lock_guard lock(g_mutex);
  return g_pak_names.contains(key);
}

}  // namespace torchlight::mods
