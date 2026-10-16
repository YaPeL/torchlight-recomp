#include "mods/replacements_guest_view.h"

namespace torchlight::mods {

std::string GuestViewKey(std::string_view path) {
  std::string key;
  key.reserve(path.size());
  for (char c : path) {
    if (c == '\\') c = '/';
    if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    if (c == '/' && key.empty()) continue;
    key += c;
  }
  return key;
}

bool IsImageFile(std::string_view path) {
  const size_t dot = path.rfind('.');
  if (dot == std::string_view::npos) return false;
  const std::string ext = GuestViewKey(path.substr(dot + 1));
  return ext == "DDS" || ext == "PNG" || ext == "TGA" || ext == "JPG" || ext == "JPEG" || ext == "BMP";
}

std::unordered_set<std::string> GuestHiddenReplacements(const std::vector<std::string>& files,
                                                        const std::function<bool(const std::string&)>& pak_has,
                                                        const std::unordered_set<std::string>& imageset_textures) {
  std::unordered_set<std::string> hidden;
  for (const std::string& file : files) {
    if (!IsImageFile(file)) continue;
    std::string key = GuestViewKey(file);
    const size_t slash = key.rfind('/');
    const std::string name = slash == std::string::npos ? key : key.substr(slash + 1);
    if (imageset_textures.contains(key) || imageset_textures.contains(name) || !pak_has(key)) continue;
    hidden.insert(std::move(key));
  }
  return hidden;
}

}  // namespace torchlight::mods
