#include "mods/pad_prompts.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <map>

#include "mods/class_list.h"
#include "save_import/pak.h"

namespace torchlight::mods {

namespace {

std::optional<long> Number(const std::string& s) {
  if (s.empty()) return std::nullopt;
  size_t used = 0;
  try {
    const long v = std::stol(s, &used);
    return used == s.size() ? std::optional<long>(v) : std::nullopt;
  } catch (...) {
    return std::nullopt;
  }
}

std::string PakKey(std::string_view path) {
  std::string key;
  for (char c : path) key += c == '\\' ? '/' : char(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
  while (!key.empty() && key.front() == '/') key.erase(key.begin());
  return key;
}

std::vector<uint8_t> ReadFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}  // namespace

PadStyle EffectivePadStyle(std::optional<PadFamily> chosen, PadKind connected) {
  PadStyle by_pad = PadStyle::kXbox;
  switch (connected) {
    case PadKind::kPlayStation3:
      by_pad = PadStyle::kPlayStation3;
      break;
    case PadKind::kPlayStation4:
      by_pad = PadStyle::kPlayStation4;
      break;
    case PadKind::kPlayStation5:
      by_pad = PadStyle::kPlayStation5;
      break;
    case PadKind::kNintendo:
      by_pad = PadStyle::kNintendo;
      break;
    default:
      break;
  }
  if (!chosen) return by_pad;
  switch (*chosen) {
    case PadFamily::kPlayStation:
      return PadIconFolder(by_pad) == "playstation" ? by_pad : PadStyle::kPlayStation5;
    case PadFamily::kNintendo:
      return PadStyle::kNintendo;
    default:
      return PadStyle::kXbox;
  }
}

bool SwapsConfirmCancel(PadStyle style, bool swap_setting) { return swap_setting && style == PadStyle::kNintendo; }

std::string_view PadStyleName(PadStyle style) {
  switch (style) {
    case PadStyle::kPlayStation3:
      return "playstation3";
    case PadStyle::kPlayStation4:
      return "playstation4";
    case PadStyle::kPlayStation5:
      return "playstation5";
    case PadStyle::kNintendo:
      return "nintendo";
    default:
      return "xbox";
  }
}

std::string_view PadIconFolder(PadStyle style) {
  switch (style) {
    case PadStyle::kPlayStation3:
    case PadStyle::kPlayStation4:
    case PadStyle::kPlayStation5:
      return "playstation";
    case PadStyle::kNintendo:
      return "nintendo";
    default:
      return "xbox";
  }
}

uint16_t SwapButtonBits(uint16_t buttons, uint16_t a_bit, uint16_t b_bit) {
  const bool a = buttons & a_bit, b = buttons & b_bit;
  buttons = uint16_t(buttons & ~(a_bit | b_bit));
  return uint16_t(buttons | (a ? b_bit : 0) | (b ? a_bit : 0));
}

uint16_t SwapVirtualKey(uint16_t key, uint16_t key_a, uint16_t key_b) {
  return key == key_a ? key_b : key == key_b ? key_a : key;
}

// The help screen's controller picture, drawn smaller than its region: a flat silhouette at the
// 360 drawing's size looks heavier than it.
constexpr double kControllerScale = 0.8;

std::vector<PromptIcon> PromptIcons(PadStyle style, bool swap_confirm_cancel) {
  std::vector<PromptIcon> icons;
  switch (style) {
    case PadStyle::kPlayStation3:
    case PadStyle::kPlayStation4:
    case PadStyle::kPlayStation5: {
      const std::string model = style == PadStyle::kPlayStation3   ? "ps3"
                                : style == PadStyle::kPlayStation4 ? "ps4"
                                                                   : "ps5";
      icons = {{"a_button", "cross"},    {"b_button", "circle"}, {"button_x", "square"},
               {"y_button", "triangle"}, {"left_bumper", "l1"},  {"right_bumper", "r1"},
               {"left_trigger", "l2"},   {"right_trigger", "r2"}};
      // Start and back as the pad's model draws them, and its controller.
      icons.push_back({"start_button", "start_" + model});
      icons.push_back({"back_button", "back_" + model});
      icons.push_back({"controller", "controller_" + model, kControllerScale});
      break;
    }
    case PadStyle::kNintendo: {
      // By position the 360's A is the bottom button, B on a Nintendo pad; swapped, the game's A
      // is the right one, A.
      const bool swap = SwapsConfirmCancel(style, swap_confirm_cancel);
      icons = {{"a_button", swap ? "a" : "b"}, {"b_button", swap ? "b" : "a"}, {"button_x", "y"},
               {"y_button", "x"},              {"left_bumper", "l"},           {"right_bumper", "r"},
               {"left_trigger", "zl"},         {"right_trigger", "zr"},        {"start_button", "plus"},
               {"back_button", "minus"},       {"controller", "controller", kControllerScale}};
      break;
    }
    default:
      return {};
  }
  // The sticks and the d-pad, the same names in both folders. The 360's sticks pushed in any
  // direction ("lrud") become the plain stick; analog_button_left_down's drawing is the right stick
  // pressed.
  const std::vector<PromptIcon> sticks = {
      {"analog_button_left_lrud", "stick_l"},           {"analog_button_right_lrud", "stick_r"},
      {"analog_button_left_lr", "stick_l_horizontal"},  {"analog_button_right_lr", "stick_r_horizontal"},
      {"analog_button_left_ud", "stick_l_vertical"},    {"analog_button_right_ud", "stick_r_vertical"},
      {"analog_button_left_down", "stick_r_press"},     {"dpad_updown", "dpad_vertical"},
      {"dpad_leftright", "dpad_horizontal"},            {"dpad_up", "dpad_up"},
      {"dpad_down", "dpad_down"},                       {"dpad_left", "dpad_left"},
      {"dpad_right", "dpad_right"}};
  icons.insert(icons.end(), sticks.begin(), sticks.end());
  return icons;
}

RgbaImage FitIcon(const RgbaImage& icon, uint32_t width, uint32_t height) {
  RgbaImage out{width, height, std::vector<uint8_t>(size_t(width) * height * 4, 0)};
  if (!icon.width || !icon.height || !width || !height) return out;
  const double scale = std::min(double(width) / icon.width, double(height) / icon.height);
  const uint32_t w = std::clamp<uint32_t>(uint32_t(std::lround(icon.width * scale)), 1, width);
  const uint32_t h = std::clamp<uint32_t>(uint32_t(std::lround(icon.height * scale)), 1, height);
  const uint32_t left = (width - w) / 2, top = (height - h) / 2;
  // Each new pixel averages the source area it covers, with partial pixels weighted, on
  // premultiplied colour so transparent pixels do not darken the edges.
  const double sx = double(icon.width) / w, sy = double(icon.height) / h;
  for (uint32_t y = 0; y < h; ++y) {
    const double y0 = y * sy, y1 = (y + 1) * sy;
    for (uint32_t x = 0; x < w; ++x) {
      const double x0 = x * sx, x1 = (x + 1) * sx;
      double sum[4] = {0, 0, 0, 0}, area = 0;
      for (uint32_t iy = uint32_t(y0); iy < icon.height && iy < y1; ++iy) {
        const double wy = std::min<double>(iy + 1, y1) - std::max<double>(iy, y0);
        for (uint32_t ix = uint32_t(x0); ix < icon.width && ix < x1; ++ix) {
          const double wx = std::min<double>(ix + 1, x1) - std::max<double>(ix, x0);
          const double weight = wx * wy;
          if (weight <= 0) continue;
          const uint8_t* p = &icon.rgba[(size_t(iy) * icon.width + ix) * 4];
          const double a = p[3] / 255.0;
          sum[0] += p[0] * a * weight;
          sum[1] += p[1] * a * weight;
          sum[2] += p[2] * a * weight;
          sum[3] += p[3] * weight;
          area += weight;
        }
      }
      uint8_t* q = &out.rgba[(size_t(top + y) * width + left + x) * 4];
      if (area <= 0 || sum[3] <= 0) continue;
      const double alpha = sum[3] / area;
      for (int c = 0; c < 3; ++c) q[c] = uint8_t(std::clamp(std::lround(sum[c] / area * 255.0 / alpha), 0L, 255L));
      q[3] = uint8_t(std::clamp(std::lround(alpha), 0L, 255L));
    }
  }
  return out;
}

std::optional<std::vector<uint8_t>> PatchPromptTexture(std::string_view imageset_xml,
                                                       const std::vector<uint8_t>& texture,
                                                       const std::vector<NamedIcon>& icons, size_t* replaced,
                                                       std::string* why) {
  auto fail = [&](std::string reason) -> std::optional<std::vector<uint8_t>> {
    if (why) *why = std::move(reason);
    return std::nullopt;
  };
  if (replaced) *replaced = 0;
  const auto atlas = ParseAtlas(imageset_xml);
  if (!atlas) return fail("the imageset does not parse");
  auto image = DecodeDds(texture);
  if (!image) return fail("its texture does not decode");
  size_t count = 0;
  for (const auto& entry : atlas->images) {
    const std::string name = atlas->Attribute(entry, "Name");
    const auto icon = std::find_if(icons.begin(), icons.end(), [&](const NamedIcon& i) { return i.image == name; });
    if (icon == icons.end()) continue;
    const auto x = Number(atlas->Attribute(entry, "XPos")), y = Number(atlas->Attribute(entry, "YPos"));
    const auto w = Number(atlas->Attribute(entry, "Width")), h = Number(atlas->Attribute(entry, "Height"));
    if (!x || !y || !w || !h || *x < 0 || *y < 0 || *w <= 0 || *h <= 0 || *x + *w > long(image->width) ||
        *y + *h > long(image->height)) {
      return fail("image " + name + " lies outside its texture");
    }
    // The region cleared, the icon fitted inside a transparent border (kPromptMargin): the game's
    // windows draw these images a little smaller than their regions and clip the bottom (the 360
    // art leaves the same border), so an icon reaching the edge would lose its last row.
    const long margin = *w > 2 * kPromptMargin && *h > 2 * kPromptMargin ? kPromptMargin : 0;
    const auto room = [&](long side) {
      return std::max<uint32_t>(1, uint32_t(std::lround(double(side - 2 * margin) * icon->scale)));
    };
    const RgbaImage fitted = FitIcon(icon->icon, room(*w), room(*h));
    const long left = *x + (*w - long(fitted.width)) / 2, top = *y + (*h - long(fitted.height)) / 2;
    for (long row = 0; row < *h; ++row) {
      std::fill_n(&image->rgba[(size_t(*y + row) * image->width + size_t(*x)) * 4], size_t(*w) * 4, uint8_t(0));
    }
    for (uint32_t row = 0; row < fitted.height; ++row) {
      std::copy_n(&fitted.rgba[size_t(row) * fitted.width * 4], fitted.width * 4,
                  &image->rgba[(size_t(top + row) * image->width + size_t(left)) * 4]);
    }
    ++count;
  }
  if (replaced) *replaced = count;
  return EncodeDdsArgb(*image);
}

std::vector<WrittenPrompts> WritePromptTextures(const std::filesystem::path& pak_path,
                                                const std::filesystem::path& icons_dir, PadStyle style,
                                                bool swap_confirm_cancel, const std::filesystem::path& out,
                                                std::vector<std::string>& log) {
  std::vector<WrittenPrompts> written;
  std::error_code ec;
  std::filesystem::remove_all(out, ec);
  const std::vector<PromptIcon> wanted = PromptIcons(style, swap_confirm_cancel);
  if (wanted.empty()) return written;
  std::vector<NamedIcon> icons;
  for (const PromptIcon& p : wanted) {
    const auto file = icons_dir / std::string(PadIconFolder(style)) / (p.file + ".dds");
    auto icon = DecodeDds(ReadFile(file));
    if (!icon) {
      log.push_back("icon " + file.string() + " missing or unreadable; " + p.image + " stays the game's");
      continue;
    }
    icons.push_back({p.image, std::move(*icon), p.scale});
  }
  if (icons.empty()) return written;
  std::string error;
  const auto pak = save_import::Pak::Open(pak_path, error);
  if (!pak) {
    log.push_back("the pak does not open: " + error);
    return written;
  }
  std::map<std::string, uint32_t> entries;  // lower-case path -> index
  pak->ForEach([&](uint32_t index, const std::string& name) {
    entries.emplace(name, index);
    return true;
  });
  for (const auto& [name, index] : entries) {
    if (!name.starts_with("media/ui/") || !name.ends_with(".imageset")) continue;
    std::vector<uint8_t> bytes;
    if (!pak->Read(index, bytes, error)) continue;
    const std::string xml = LayoutText(bytes);
    const auto atlas = ParseAtlas(xml);
    if (!atlas) continue;
    const bool has = std::any_of(atlas->images.begin(), atlas->images.end(), [&](const auto& image) {
      const std::string n = atlas->Attribute(image, "Name");
      return std::any_of(icons.begin(), icons.end(), [&](const NamedIcon& i) { return i.image == n; });
    });
    if (!has) continue;
    const std::string texture_name = atlas->Attribute(atlas->imageset, "Imagefile");
    const auto texture = entries.find(PakKey(texture_name));
    std::vector<uint8_t> texture_bytes;
    if (texture == entries.end() || !pak->Read(texture->second, texture_bytes, error)) {
      log.push_back(name + ": its texture is not in the pak; its prompts stay the game's");
      continue;
    }
    size_t replaced = 0;
    std::string why;
    const auto patched = PatchPromptTexture(xml, texture_bytes, icons, &replaced, &why);
    if (!patched) {
      log.push_back(name + ": " + why + "; its prompts stay the game's");
      continue;
    }
    const std::filesystem::path file = out / PakKey(texture_name);
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream stream(file, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(patched->data()), std::streamsize(patched->size()));
    if (!stream) {
      log.push_back(file.string() + ": cannot be written; its prompts stay the game's");
      continue;
    }
    written.push_back({PakKey(texture_name), replaced});
  }
  return written;
}

}  // namespace torchlight::mods
