// Reading the .adm definitions inside a game pak (pak.zip on the 360, Pak.zip on PC): the zip,
// through miniz with our own read callback (std::filesystem paths on every platform), and the
// .adm format (see tools/save_convert/gamedata.py).

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace torchlight::save_import {

// An .adm node: name, properties and children. Texts are UTF-8.
struct AdmNode {
  struct Property {
    std::string key;
    uint32_t type = 0;  // 1 int32, 2 float, 3 double, 4 uint32, 5 string, 6 bool, 7 int64, 8 text
    std::variant<int64_t, double, std::string> value;
  };
  std::string name;
  std::vector<Property> properties;
  std::vector<AdmNode> children;

  const Property* Find(std::string_view key) const;
};

// Parses an .adm file; false with `error` set if it is damaged or has bytes left over.
bool ParseAdm(const std::vector<uint8_t>& data, AdmNode& root, std::string& error);

class Pak {
 public:
  ~Pak();
  // Opens the zip; null with `error` set if it cannot be read as one.
  static std::unique_ptr<Pak> Open(const std::filesystem::path& path, std::string& error);

  // Calls `visit` with the name (lower case, '/' separators) of every file entry. Stops at the
  // first false it returns.
  void ForEach(const std::function<bool(uint32_t index, const std::string& name)>& visit) const;
  // The contents of entry `index`; false with `error` set if it cannot be extracted.
  bool Read(uint32_t index, std::vector<uint8_t>& out, std::string& error) const;

 private:
  Pak() = default;
  struct State;
  std::unique_ptr<State> state_;
};

// UTF-16 code units to UTF-8 (unpaired surrogates kept as their three-byte form, so every text
// round-trips like the Python tool's surrogatepass).
std::string Utf8(std::u16string_view text);
// UTF-8 to UTF-16 code units (invalid bytes become U+FFFD).
std::u16string Utf16(std::string_view utf8);
// How Python's repr() shows a str (the Python tool's messages use it): quoted, with escapes.
std::string PythonRepr(std::string_view text);
// ASCII letters to upper case; other bytes unchanged (the game's identifiers are ASCII).
std::string Upper(std::string_view text);

}  // namespace torchlight::save_import
