#include "game_menu/texture_open.h"

#include <array>
#include <cctype>

namespace torchlight::game_menu {

bool IsTextureName(std::string_view name) {
  static constexpr std::array<std::string_view, 6> kExtensions = {".DDS", ".PNG", ".TGA",
                                                                  ".JPG", ".JPEG", ".BMP"};
  for (std::string_view ext : kExtensions) {
    if (name.size() <= ext.size()) continue;
    const std::string_view tail = name.substr(name.size() - ext.size());
    bool same = true;
    for (size_t i = 0; i < ext.size() && same; ++i) {
      same = std::toupper(static_cast<unsigned char>(tail[i])) == ext[i];
    }
    if (same) return true;
  }
  return false;
}

void TextureOpenRecorder::Begin(std::string_view name, uint32_t stack) {
  if (depth_ > 0 && stack >= outer_stack_) depth_ = 0;
  if (depth_++ == 0) {
    outer_stack_ = stack;
    name_ = name;
    opened_.reset();
  }
}

void TextureOpenRecorder::ArchiveOpened(std::string_view container, std::string_view path) {
  if (depth_ > 0) opened_.emplace(std::string(container), std::string(path));
}

std::optional<TextureOpen> TextureOpenRecorder::End() {
  if (depth_ == 0 || --depth_ > 0) return std::nullopt;
  if (!opened_ || !IsTextureName(name_)) return std::nullopt;
  return TextureOpen{name_, opened_->first, opened_->second};
}

}  // namespace torchlight::game_menu
