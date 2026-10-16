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
  if (!locations.prompts.empty()) prompts_ = List(locations.prompts);
  if (!locations.generated.empty() && !locations.generated_device.empty()) {
    generated_ = List(locations.generated);
    generated_device_ = TextureNameKey(locations.generated_device);
  }
  for (const auto& mod : locations.mods) {
    mods_.emplace_back(mod.group, List(mod.folder));
    devices_.push_back(TextureNameKey(mod.device));
  }
}

TextureFile TextureResolver::Find(std::string_view name, std::string_view group,
                                  const std::function<bool(std::string_view)>& pak_has) const {
  const std::string key = TextureNameKey(name);
  // An absolute name (a device first): only that device's folder.
  if (const size_t colon = key.find(':'); colon != std::string::npos) {
    std::string rest = key.substr(colon + 1);
    while (!rest.empty() && rest.front() == '/') rest.erase(rest.begin());
    const std::string device = key.substr(0, colon + 1);
    if (device == "TLREPL:") {
      if (replacements_) {
        if (auto file = replacements_->Find(rest)) return {TextureSource::kReplacements, *file};
      }
      return {};
    }
    if (device == "TLPROMPT:") {
      if (prompts_) {
        if (auto file = prompts_->Find(rest)) return {TextureSource::kGenerated, *file};
      }
      return {};
    }
    if (generated_ && device == generated_device_) {
      if (auto file = generated_->Find(rest)) return {TextureSource::kGenerated, *file};
      return {};
    }
    for (size_t i = 0; i < devices_.size() && i < mods_.size(); ++i) {
      if (devices_[i] != device) continue;
      if (auto file = mods_[i].second.Find(rest)) return {TextureSource::kMod, *file};
      return {};
    }
    return {};
  }
  auto game = [&]() -> std::optional<TextureFile> {
    if (replacements_) {
      if (auto file = replacements_->Find(key)) return TextureFile{TextureSource::kReplacements, *file};
    }
    if (prompts_) {
      if (auto file = prompts_->Find(key)) return TextureFile{TextureSource::kGenerated, *file};
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
std::unordered_set<std::string> g_pak_names;
}  // namespace

void SetTextureLocations(const TextureLocations& locations) {
  auto resolver = std::make_shared<const TextureResolver>(locations);
  std::lock_guard lock(g_mutex);
  g_resolver = resolver->empty() ? nullptr : resolver;
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
