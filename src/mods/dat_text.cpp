#include "mods/dat_text.h"

#include <algorithm>
#include <cctype>

namespace torchlight::mods {

namespace {

bool EqualNoCase(std::string_view a, std::string_view b) {
  return a.size() == b.size() &&
         std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
           return std::toupper(static_cast<unsigned char>(x)) == std::toupper(static_cast<unsigned char>(y));
         });
}

void AppendUtf8(std::string& out, uint32_t cp) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

// The text as UTF-8: UTF-16LE when it starts with FF FE, else the bytes (UTF-8 or ASCII), without
// a UTF-8 byte order mark.
std::string Decode(const std::vector<uint8_t>& bytes) {
  std::string out;
  if (bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
    for (size_t i = 2; i + 1 < bytes.size(); i += 2) {
      uint32_t unit = bytes[i] | (bytes[i + 1] << 8);
      if (unit >= 0xD800 && unit < 0xDC00 && i + 3 < bytes.size()) {
        const uint32_t low = bytes[i + 2] | (bytes[i + 3] << 8);
        if (low >= 0xDC00 && low < 0xE000) {
          unit = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
          i += 2;
        }
      }
      AppendUtf8(out, unit);
    }
    return out;
  }
  size_t start = 0;
  if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) start = 3;
  return std::string(bytes.begin() + static_cast<long>(start), bytes.end());
}

std::string_view Trim(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
  return s;
}

void AppendUtf16(std::vector<uint8_t>& out, std::string_view utf8) {
  for (size_t i = 0; i < utf8.size();) {
    const auto c = static_cast<unsigned char>(utf8[i]);
    uint32_t cp = c;
    size_t n = 1;
    if (c >= 0xF0 && i + 3 < utf8.size()) {
      cp = ((c & 0x07) << 18) | ((utf8[i + 1] & 0x3F) << 12) | ((utf8[i + 2] & 0x3F) << 6) | (utf8[i + 3] & 0x3F);
      n = 4;
    } else if (c >= 0xE0 && i + 2 < utf8.size()) {
      cp = ((c & 0x0F) << 12) | ((utf8[i + 1] & 0x3F) << 6) | (utf8[i + 2] & 0x3F);
      n = 3;
    } else if (c >= 0xC0 && i + 1 < utf8.size()) {
      cp = ((c & 0x1F) << 6) | (utf8[i + 1] & 0x3F);
      n = 2;
    }
    i += n;
    auto unit = [&out](uint32_t u) {
      out.push_back(static_cast<uint8_t>(u & 0xFF));
      out.push_back(static_cast<uint8_t>(u >> 8));
    };
    if (cp >= 0x10000) {
      cp -= 0x10000;
      unit(0xD800 + (cp >> 10));
      unit(0xDC00 + (cp & 0x3FF));
    } else {
      unit(cp);
    }
  }
}

void WriteBlock(std::vector<uint8_t>& out, const DatBlock& block, int depth) {
  const std::string indent(static_cast<size_t>(depth), '\t');
  AppendUtf16(out, indent + "[" + block.name + "]\r\n");
  for (const DatValue& v : block.values) {
    AppendUtf16(out, indent + "\t<" + v.type + ">" + v.key + ":" + v.value + "\r\n");
  }
  for (const DatBlock& child : block.children) WriteBlock(out, child, depth + 1);
  AppendUtf16(out, indent + "[/" + block.name + "]\r\n");
}

}  // namespace

const DatValue* DatBlock::Find(std::string_view key) const {
  for (const DatValue& v : values) {
    if (EqualNoCase(v.key, key)) return &v;
  }
  return nullptr;
}

std::optional<std::vector<DatBlock>> ParseDatText(const std::vector<uint8_t>& bytes, std::string* error) {
  const std::string text = Decode(bytes);
  std::vector<DatBlock> roots;
  std::vector<DatBlock> open;  // the blocks being read, innermost last
  size_t line_number = 0;
  auto fail = [&](const std::string& why) -> std::optional<std::vector<DatBlock>> {
    if (error) *error = "line " + std::to_string(line_number) + ": " + why;
    return std::nullopt;
  };
  size_t start = 0;
  while (start <= text.size()) {
    // A line ends at LF, CR LF or a lone CR: PC tools left lone CRs between tags
    // ("[/EFFECT]\r[/EFFECTS]" in the Mod-Pack's JCC - Vindicator), and the game reads those as
    // two lines (its own index build loads such files).
    size_t end = text.find_first_of("\r\n", start);
    if (end == std::string::npos) end = text.size();
    const size_t next = end < text.size() && text[end] == '\r' && end + 1 < text.size() && text[end + 1] == '\n'
                            ? end + 2
                            : end + 1;
    ++line_number;
    const std::string_view line = Trim(std::string_view(text).substr(start, end - start));
    start = next;
    if (line.empty() || line.starts_with("//")) continue;  // a comment, as PC mods' files have
    if (line.size() >= 3 && line.front() == '[' && line.back() == ']') {
      if (line[1] == '/') {
        // Tolerant, as the game is with PC mods' files (its own loading keeps them): a closing tag
        // that matches no open block is skipped (an extra "[/EFFECT]" in the Mod-Pack's JCC - Pets
        // dfb_pet_lich.dat), and one that matches an outer block closes the inner ones with it.
        const std::string_view name = line.substr(2, line.size() - 3);
        const auto match = std::find_if(open.rbegin(), open.rend(),
                                        [&](const DatBlock& b) { return EqualNoCase(b.name, name); });
        if (match == open.rend()) continue;
        const size_t depth = static_cast<size_t>(match - open.rbegin()) + 1;
        for (size_t i = 0; i < depth; ++i) {
          DatBlock done = std::move(open.back());
          open.pop_back();
          if (open.empty()) {
            roots.push_back(std::move(done));
          } else {
            open.back().children.push_back(std::move(done));
          }
        }
      } else {
        open.push_back(DatBlock{std::string(line.substr(1, line.size() - 2)), {}, {}});
      }
      continue;
    }
    if (line.front() == '<') {
      const size_t close = line.find('>');
      const size_t colon = close == std::string_view::npos ? close : line.find(':', close);
      if (close == std::string_view::npos || colon == std::string_view::npos) {
        return fail("value line without <TYPE>KEY:VALUE");
      }
      if (open.empty()) return fail("value outside any block");
      DatValue value;
      value.type = std::string(Trim(line.substr(1, close - 1)));
      std::transform(value.type.begin(), value.type.end(), value.type.begin(),
                     [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
      value.key = std::string(Trim(line.substr(close + 1, colon - close - 1)));
      value.value = std::string(line.substr(colon + 1));
      open.back().values.push_back(std::move(value));
      continue;
    }
    return fail("not a block tag or a value line");
  }
  // Blocks left open at the end are closed there (a set file of the Mod-Pack ends without its
  // "[/SET]"), as the game, which loads it, does.
  while (!open.empty()) {
    DatBlock done = std::move(open.back());
    open.pop_back();
    if (open.empty()) {
      roots.push_back(std::move(done));
    } else {
      open.back().children.push_back(std::move(done));
    }
  }
  return roots;
}

std::optional<std::string> FindDatValueAnywhere(const std::vector<uint8_t>& bytes, std::string_view key) {
  const std::string text = Decode(bytes);
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find_first_of("\r\n", start);
    if (end == std::string::npos) end = text.size();
    const std::string_view line = Trim(std::string_view(text).substr(start, end - start));
    start = end + 1;
    if (line.empty() || line.front() != '<') continue;
    const size_t close = line.find('>');
    const size_t colon = close == std::string_view::npos ? close : line.find(':', close);
    if (colon == std::string_view::npos) continue;
    if (EqualNoCase(Trim(line.substr(close + 1, colon - close - 1)), key)) {
      return std::string(line.substr(colon + 1));
    }
  }
  return std::nullopt;
}

std::vector<uint8_t> WriteDatText(const std::vector<DatBlock>& blocks) {
  std::vector<uint8_t> out = {0xFF, 0xFE};
  for (const DatBlock& block : blocks) WriteBlock(out, block, 0);
  return out;
}

}  // namespace torchlight::mods
