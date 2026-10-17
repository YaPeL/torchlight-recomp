#include "dev/ui_tree.h"

#include <cmath>
#include <cstdio>

namespace torchlight::dev {

std::optional<UiImage> ParseImageProperty(std::string_view value) {
  const size_t set = value.find("set:");
  const size_t image = value.find("image:");
  if (set == std::string_view::npos || image == std::string_view::npos || image < set) return std::nullopt;
  std::string_view imageset = value.substr(set + 4, image - set - 4);
  while (!imageset.empty() && imageset.back() == ' ') imageset.remove_suffix(1);
  std::string_view name = value.substr(image + 6);
  while (!name.empty() && name.back() == ' ') name.remove_suffix(1);
  if (imageset.empty() || name.empty()) return std::nullopt;
  return UiImage{std::string(imageset), std::string(name)};
}

std::string Utf32ToUtf8(const std::vector<uint32_t>& code_points) {
  std::string out;
  for (uint32_t c : code_points) {
    if (c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) c = 0xFFFD;
    if (c < 0x80) {
      out.push_back(char(c));
    } else if (c < 0x800) {
      out.push_back(char(0xC0 | (c >> 6)));
      out.push_back(char(0x80 | (c & 0x3F)));
    } else if (c < 0x10000) {
      out.push_back(char(0xE0 | (c >> 12)));
      out.push_back(char(0x80 | ((c >> 6) & 0x3F)));
      out.push_back(char(0x80 | (c & 0x3F)));
    } else {
      out.push_back(char(0xF0 | (c >> 18)));
      out.push_back(char(0x80 | ((c >> 12) & 0x3F)));
      out.push_back(char(0x80 | ((c >> 6) & 0x3F)));
      out.push_back(char(0x80 | (c & 0x3F)));
    }
  }
  return out;
}

namespace {

std::string Quote(std::string_view s) {
  std::string out = "\"";
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out.push_back(char(c));
        }
    }
  }
  return out + "\"";
}

std::string Number(double v) {
  if (!std::isfinite(v)) return "null";
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.3f", v);
  std::string s = buf;
  while (s.back() == '0') s.pop_back();
  if (s.back() == '.') s.pop_back();
  return s == "-0" ? "0" : s;
}

std::string Rect(const UiRect& r, double scale) {
  return "{\"left\": " + Number(r.left * scale) + ", \"top\": " + Number(r.top * scale) +
         ", \"right\": " + Number(r.right * scale) + ", \"bottom\": " + Number(r.bottom * scale) +
         ", \"width\": " + Number((r.right - r.left) * scale) + ", \"height\": " +
         Number((r.bottom - r.top) * scale) + "}";
}

void Window(std::string& out, const UiWindow& w, double scale, const std::string& pad) {
  const std::string in = pad + "  ";
  out += pad + "{\n";
  out += in + "\"name\": " + Quote(w.name) + ",\n";
  out += in + "\"type\": " + Quote(w.type) + ",\n";
  out += in + "\"class\": " + Quote(w.class_name) + ",\n";
  out += in + "\"visible\": " + (w.visible ? "true" : "false") + ",\n";
  out += in + "\"shown\": " + (w.shown ? "true" : "false") + ",\n";
  out += in + "\"rect\": " + Rect(w.rect, 1) + ",\n";
  out += in + "\"rect_1080p\": " + Rect(w.rect, scale) + ",\n";
  out += in + "\"clipped\": " + Rect(w.clipped, 1) + ",\n";
  out += in + "\"clipped_1080p\": " + Rect(w.clipped, scale) + ",\n";
  out += in + "\"alpha\": " + (w.alpha ? Number(*w.alpha) : std::string("null")) + ",\n";
  out += in + "\"effective_alpha\": " + Number(w.effective_alpha) + ",\n";
  out += in + "\"font\": " + Quote(w.font) + ",\n";
  out += in + "\"text\": " + Quote(w.text) + ",\n";
  out += in + "\"images\": {";
  bool first = true;
  for (const auto& [property, image] : w.images) {
    out += std::string(first ? "" : ", ") + Quote(property) + ": {\"imageset\": " + Quote(image.imageset) +
           ", \"image\": " + Quote(image.image) + "}";
    first = false;
  }
  out += "},\n";
  out += in + "\"properties\": {";
  first = true;
  for (const auto& [property, value] : w.properties) {
    out += std::string(first ? "" : ", ") + Quote(property) + ": " + Quote(value);
    first = false;
  }
  out += "},\n";
  out += in + "\"children\": [";
  if (w.children.empty()) {
    out += "]\n";
  } else {
    out += "\n";
    for (size_t i = 0; i < w.children.size(); ++i) {
      Window(out, w.children[i], scale, in + "  ");
      out += i + 1 < w.children.size() ? ",\n" : "\n";
    }
    out += in + "]\n";
  }
  out += pad + "}";
}

}  // namespace

std::string UiDumpJson(const UiDump& d) {
  const double scale = d.display_height > 0 ? 1080.0 / d.display_height : 1.0;
  std::string out = "{\n";
  out += "  \"format\": \"torchlight-ui-dump 1\",\n";
  out += "  \"name\": " + Quote(d.name) + ",\n";
  out += "  \"frame\": " + std::to_string(d.frame) + ",\n";
  out += "  \"display\": {\"width\": " + Number(d.display_width) + ", \"height\": " + Number(d.display_height) +
         "},\n";
  out += "  \"scale_to_1080p\": " + Number(scale) + ",\n";
  out += "  \"menus\": [";
  for (size_t i = 0; i < d.menus.size(); ++i) {
    const UiMenu& m = d.menus[i];
    out += std::string(i ? ", " : "") + "{\"class\": " + Quote(m.class_name) + ", \"root\": " + Quote(m.root) +
           ", \"focus\": " + Quote(m.focus) + "}";
  }
  out += "],\n";
  out += "  \"windows\": [";
  if (d.windows.empty()) {
    out += "]\n";
  } else {
    out += "\n";
    for (size_t i = 0; i < d.windows.size(); ++i) {
      Window(out, d.windows[i], scale, "    ");
      out += i + 1 < d.windows.size() ? ",\n" : "\n";
    }
    out += "  ]\n";
  }
  return out + "}\n";
}

}  // namespace torchlight::dev
