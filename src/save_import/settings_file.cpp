#include "save_import/settings_file.h"

#include <cerrno>
#include <cstdlib>

#include "save_import/pak.h"

namespace torchlight::save_import {
namespace {

constexpr uint8_t kBom[] = {0xFF, 0xFE};

// User preferences that mean the same on both platforms, and progress flags that only go up, each
// under the 360 file that holds it (tools/save_convert/settings.py explains the choice).
struct Rules {
  std::vector<std::string_view> preferences, progress;
};

const Rules& RulesFor(std::string_view name) {
  static const Rules kSettings{{"AUTOMAP", "SHOW BLOOD", "NO CAMERA SHAKE", "AUTOMAP ZOOM"}, {}};
  static const Rules kLocal{{"SOUND VOLUME", "MUSIC VOLUME", "SOUND MUTE", "MUSIC MUTE",
                             "SHOW TIPS", "FLOATY_NUMBERS"},
                            {"GAME_COMPLETED_ONCE"}};
  return name == "settings.txt" ? kSettings : kLocal;
}

// Python's float(): the value as a number, or none (hexadecimal is not accepted, as in Python).
std::optional<double> Number(const std::string& value) {
  size_t begin = value.find_first_not_of(" \t\r\n\f\v");
  size_t end = value.find_last_not_of(" \t\r\n\f\v");
  if (begin == std::string::npos) return std::nullopt;
  const std::string trimmed = value.substr(begin, end - begin + 1);
  if (trimmed.find_first_of("xX") != std::string::npos) return std::nullopt;
  char* stop = nullptr;
  errno = 0;
  const double number = std::strtod(trimmed.c_str(), &stop);
  if (stop != trimmed.c_str() + trimmed.size()) return std::nullopt;
  return number;
}

bool Decode(std::span<const uint8_t> body, bool big_endian, std::string& out) {
  out.clear();
  for (size_t i = 0; i + 1 < body.size(); i += 2) {
    const uint16_t unit = big_endian ? uint16_t(body[i] << 8 | body[i + 1])
                                     : uint16_t(body[i] | body[i + 1] << 8);
    if (!(unit == '\n' || unit == '\r' || unit == '\t' || (unit >= 32 && unit < 127))) return false;
    out += static_cast<char>(unit);
  }
  return true;
}

}  // namespace

bool SettingsFile::Parse(std::span<const uint8_t> data, std::string_view what, SettingsFile& out,
                         std::string& error) {
  const std::string name(what);
  if (data.size() < 2 || data[0] != kBom[0] || data[1] != kBom[1]) {
    error = name + " does not start with the FF FE mark of a Torchlight settings file";
    return false;
  }
  const auto body = data.subspan(2);
  if (body.size() % 2) {
    error = name + " has an odd number of bytes after its mark";
    return false;
  }
  std::string big, little;
  const bool is_big = Decode(body, true, big);
  const bool is_little = Decode(body, false, little);
  if (is_big == is_little) {
    error = name + " is not plain \"KEY :value\" text in UTF-16 (byte order unclear)";
    return false;
  }
  const std::string& text = is_big ? big : little;
  if (!text.empty() && text.back() != '\n') {
    error = name + " does not end with a line break";
    return false;
  }
  out.big_endian_ = is_big;
  out.lines_.clear();
  size_t start = 0;
  while (start < text.size()) {
    const size_t end = text.find('\n', start);
    out.lines_.push_back(text.substr(start, end - start));
    start = end + 1;
  }
  return true;
}

SettingsFile SettingsFile::New360() { return SettingsFile(); }

Bytes SettingsFile::ToBytes() const {
  Bytes out(std::begin(kBom), std::end(kBom));
  auto put = [&](char c) {
    if (big_endian_) {
      out.push_back(0);
      out.push_back(static_cast<uint8_t>(c));
    } else {
      out.push_back(static_cast<uint8_t>(c));
      out.push_back(0);
    }
  };
  for (const auto& line : lines_) {
    for (char c : line) put(c);
    put('\n');
  }
  return out;
}

std::optional<std::string> SettingsFile::Get(std::string_view key) const {
  for (const auto& line : lines_) {
    const size_t separator = line.find(" :");
    if (separator == std::string::npos || std::string_view(line).substr(0, separator) != key) continue;
    std::string value = line.substr(separator + 2);
    while (!value.empty() && value.back() == '\r') value.pop_back();
    return value;
  }
  return std::nullopt;
}

void SettingsFile::Set(std::string_view key, std::string_view value) {
  const std::string line = std::string(key) + " :" + std::string(value);
  for (auto& existing : lines_) {
    const size_t separator = existing.find(" :");
    if (separator == std::string::npos || std::string_view(existing).substr(0, separator) != key) continue;
    const bool crlf = !existing.empty() && existing.back() == '\r';
    existing = line + (crlf ? "\r" : "");
    return;
  }
  lines_.push_back(line);
}

SettingsPlan PlanSettingsImport(std::string_view name, const std::vector<SettingsFile>& pc,
                                const SettingsFile* recomp, bool force) {
  SettingsPlan plan;
  const Rules& rules = RulesFor(name);
  const std::string file(name);
  auto consider = [&](std::string_view key, bool progress) {
    std::optional<std::string> value;
    for (const auto& source : pc) {
      if ((value = source.Get(key))) break;
    }
    if (!value) return;
    const std::string k(key);
    if (!Number(*value)) {
      plan.problems.push_back(k + " in the PC " + file + " is not a number: " + PythonRepr(*value));
      return;
    }
    const std::optional<std::string> old = recomp ? recomp->Get(key) : std::nullopt;
    if (old == value) return;
    if (progress) {
      if (old && !Number(*old)) {
        plan.problems.push_back(k + " in the recomp " + file + " is not a number: " + PythonRepr(*old));
      } else if (!old || *Number(*value) > *Number(*old)) {
        plan.changes.push_back({k, old, *value});
      }
      return;
    }
    if (!recomp || !old || force) plan.changes.push_back({k, old, *value});
    else plan.skipped.push_back({k, old, *value});
  };
  for (auto key : rules.preferences) consider(key, false);
  for (auto key : rules.progress) consider(key, true);
  return plan;
}

}  // namespace torchlight::save_import
