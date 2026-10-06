#include "save_import/json.h"

#include <cctype>
#include <charconv>

namespace torchlight::save_import {

class JsonParser {
 public:
  explicit JsonParser(std::string_view text) : text_(text) {}

  std::unique_ptr<Json> Document(std::string& error) {
    auto value = Value();
    SkipSpace();
    if (value && pos_ != text_.size()) Fail("trailing characters");
    if (!error_.empty()) {
      error = error_ + " at byte " + std::to_string(pos_);
      return nullptr;
    }
    return value;
  }

 private:
  void Fail(const char* what) {
    if (error_.empty()) error_ = what;
  }
  void SkipSpace() {
    while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) ++pos_;
  }
  bool Take(char c) {
    SkipSpace();
    if (pos_ < text_.size() && text_[pos_] == c) {
      ++pos_;
      return true;
    }
    return false;
  }
  bool Word(std::string_view word) {
    if (text_.substr(pos_, word.size()) != word) return false;
    pos_ += word.size();
    return true;
  }

  std::unique_ptr<Json> Value() {
    if (++depth_ > 64) {
      Fail("nested too deeply");
      return nullptr;
    }
    SkipSpace();
    auto value = std::make_unique<Json>();
    if (pos_ >= text_.size()) {
      Fail("unexpected end");
    } else if (text_[pos_] == '{') {
      ++pos_;
      value->kind_ = Json::Kind::kObject;
      if (!Take('}')) {
        do {
          SkipSpace();
          std::string key;
          if (!String(key)) break;
          if (!Take(':')) {
            Fail("expected ':'");
            break;
          }
          auto member = Value();
          if (!member) break;
          value->members_.emplace_back(std::move(key), std::move(member));
        } while (Take(','));
        if (error_.empty() && !Take('}')) Fail("expected '}'");
      }
    } else if (text_[pos_] == '[') {
      ++pos_;
      value->kind_ = Json::Kind::kArray;
      if (!Take(']')) {
        do {
          auto item = Value();
          if (!item) break;
          value->items_.push_back(std::move(item));
        } while (Take(','));
        if (error_.empty() && !Take(']')) Fail("expected ']'");
      }
    } else if (text_[pos_] == '"') {
      value->kind_ = Json::Kind::kString;
      String(value->string_);
    } else if (Word("true") || Word("false")) {
      value->kind_ = Json::Kind::kBool;
      value->integer_ = text_[pos_ - 1] == 'e' && text_[pos_ - 2] == 'u' ? 1 : 0;
    } else if (Word("null")) {
      value->kind_ = Json::Kind::kNull;
    } else {
      value->kind_ = Json::Kind::kInteger;
      const char* begin = text_.data() + pos_;
      const char* end = text_.data() + text_.size();
      auto [next, ec] = std::from_chars(begin, end, value->integer_);
      if (ec != std::errc() || (next < end && (*next == '.' || *next == 'e' || *next == 'E'))) {
        Fail("expected a value (only integers are supported)");
      } else {
        pos_ += next - begin;
      }
    }
    --depth_;
    if (!error_.empty()) return nullptr;
    return value;
  }

  bool String(std::string& out) {
    if (pos_ >= text_.size() || text_[pos_] != '"') {
      Fail("expected a string");
      return false;
    }
    ++pos_;
    while (pos_ < text_.size() && text_[pos_] != '"') {
      char c = text_[pos_++];
      if (c != '\\') {
        out += c;
        continue;
      }
      if (pos_ >= text_.size()) break;
      char escape = text_[pos_++];
      switch (escape) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
          unsigned code = 0;
          if (pos_ + 4 > text_.size() ||
              std::from_chars(text_.data() + pos_, text_.data() + pos_ + 4, code, 16).ptr !=
                  text_.data() + pos_ + 4 ||
              (code >= 0xD800 && code < 0xE000)) {
            Fail("unsupported \\u escape");
            return false;
          }
          pos_ += 4;
          if (code < 0x80) {
            out += static_cast<char>(code);
          } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
          } else {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
          }
          break;
        }
        default:
          Fail("unknown escape");
          return false;
      }
    }
    if (pos_ >= text_.size()) {
      Fail("unterminated string");
      return false;
    }
    ++pos_;
    return true;
  }

  std::string_view text_;
  size_t pos_ = 0;
  int depth_ = 0;
  std::string error_;
};

std::unique_ptr<Json> Json::Parse(std::string_view text, std::string& error) {
  return JsonParser(text).Document(error);
}

const Json* Json::Find(std::string_view key) const {
  for (const auto& [name, value] : members_) {
    if (name == key) return value.get();
  }
  return nullptr;
}

}  // namespace torchlight::save_import
