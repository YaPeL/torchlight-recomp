// A small JSON reader for the save schema (tools/save_convert/character_save.schema.json): objects
// (key order kept), arrays, strings, integers, booleans and null. Enough for that file; not a
// general JSON library (no floating point beyond integers, \u escapes limited to the BMP).

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace torchlight::save_import {

class Json {
 public:
  enum class Kind { kNull, kBool, kInteger, kString, kArray, kObject };

  // Parses `text`; on error returns null and sets `error` (with the byte offset).
  static std::unique_ptr<Json> Parse(std::string_view text, std::string& error);

  Kind kind() const { return kind_; }
  bool is_object() const { return kind_ == Kind::kObject; }
  bool is_array() const { return kind_ == Kind::kArray; }
  bool is_string() const { return kind_ == Kind::kString; }
  bool is_integer() const { return kind_ == Kind::kInteger; }

  int64_t integer() const { return integer_; }
  bool boolean() const { return integer_ != 0; }
  const std::string& string() const { return string_; }
  const std::vector<std::unique_ptr<Json>>& items() const { return items_; }
  const std::vector<std::pair<std::string, std::unique_ptr<Json>>>& members() const {
    return members_;
  }
  // The member called `key`, or null.
  const Json* Find(std::string_view key) const;

 private:
  friend class JsonParser;
  Kind kind_ = Kind::kNull;
  int64_t integer_ = 0;
  std::string string_;
  std::vector<std::unique_ptr<Json>> items_;
  std::vector<std::pair<std::string, std::unique_ptr<Json>>> members_;
};

}  // namespace torchlight::save_import
