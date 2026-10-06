#include "save_import/pak.h"

#include <miniz.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>

namespace torchlight::save_import {

std::string Utf8(std::u16string_view text) {
  std::string out;
  for (size_t i = 0; i < text.size(); ++i) {
    uint32_t code = text[i];
    if (code >= 0xD800 && code < 0xDC00 && i + 1 < text.size() && text[i + 1] >= 0xDC00 &&
        text[i + 1] < 0xE000) {
      code = 0x10000 + ((code - 0xD800) << 10) + (text[i + 1] - 0xDC00);
      ++i;
    }
    if (code < 0x80) {
      out += static_cast<char>(code);
    } else if (code < 0x800) {
      out += static_cast<char>(0xC0 | (code >> 6));
      out += static_cast<char>(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
      out += static_cast<char>(0xE0 | (code >> 12));
      out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (code & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (code >> 18));
      out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (code & 0x3F));
    }
  }
  return out;
}

// Python's repr() of a str: quotes, and backslash escapes for what is not printable ASCII-wise.
std::u16string Utf16(std::string_view utf8) {
  std::u16string out;
  for (size_t i = 0; i < utf8.size();) {
    const unsigned char lead = static_cast<unsigned char>(utf8[i]);
    uint32_t code = 0xFFFD;
    size_t length = 1;
    if (lead < 0x80) {
      code = lead;
    } else if (lead >= 0xC2 && lead < 0xF5) {
      length = lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
      if (i + length <= utf8.size()) {
        code = lead & (0xFF >> (length + 1));
        for (size_t k = 1; k < length; ++k) {
          const unsigned char next = static_cast<unsigned char>(utf8[i + k]);
          if ((next & 0xC0) != 0x80) {
            code = 0xFFFD;
            length = k;
            break;
          }
          code = (code << 6) | (next & 0x3F);
        }
      } else {
        length = utf8.size() - i;
      }
    }
    if (code >= 0x10000 && code <= 0x10FFFF) {
      code -= 0x10000;
      out.push_back(static_cast<char16_t>(0xD800 + (code >> 10)));
      out.push_back(static_cast<char16_t>(0xDC00 + (code & 0x3FF)));
    } else {
      out.push_back(static_cast<char16_t>(code > 0x10FFFF ? 0xFFFD : code));
    }
    i += length;
  }
  return out;
}

std::string PythonRepr(std::string_view text) {
  const bool has_single = text.find('\'') != std::string_view::npos;
  const bool has_double = text.find('"') != std::string_view::npos;
  const char quote = has_single && !has_double ? '"' : '\'';
  std::string out(1, quote);
  for (unsigned char c : text) {
    if (c == '\\') out += "\\\\";
    else if (c == static_cast<unsigned char>(quote)) out += std::string("\\") + quote;
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else if (c == '\t') out += "\\t";
    else if (c < 0x20 || c == 0x7F) {
      char escape[5];
      std::snprintf(escape, sizeof(escape), "\\x%02x", c);
      out += escape;
    } else out += static_cast<char>(c);
  }
  return out + quote;
}

std::string Upper(std::string_view text) {
  std::string out(text);
  for (char& c : out) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  }
  return out;
}

const AdmNode::Property* AdmNode::Find(std::string_view key) const {
  for (const auto& property : properties) {
    if (property.key == key) return &property;
  }
  return nullptr;
}

namespace {

class AdmReader {
 public:
  explicit AdmReader(const std::vector<uint8_t>& data) : data_(data) {}

  bool Root(AdmNode& root, std::string& error) {
    uint32_t count = 0;
    if (!U32(count) || !U32(count)) return Fail(error);  // version, then the string count
    for (uint32_t i = 0; i < count; ++i) {
      uint32_t id = 0, length = 0;
      if (!U32(id) || !U32(length) || length > (data_.size() - pos_) / 2) return Fail(error);
      std::u16string text(length, u'\0');
      for (uint32_t c = 0; c < length; ++c) {
        text[c] = static_cast<char16_t>(data_[pos_] | data_[pos_ + 1] << 8);
        pos_ += 2;
      }
      strings_[id] = Utf8(text);
    }
    if (!Node(root, 0)) return Fail(error);
    if (pos_ != data_.size()) {
      error = "adm with " + std::to_string(data_.size() - pos_) + " leftover bytes";
      return false;
    }
    return true;
  }

 private:
  bool Fail(std::string& error) {
    error = error_.empty() ? "truncated adm" : error_;
    return false;
  }
  bool U32(uint32_t& out) {
    if (data_.size() - pos_ < 4) return false;
    out = uint32_t(data_[pos_]) | uint32_t(data_[pos_ + 1]) << 8 | uint32_t(data_[pos_ + 2]) << 16 |
          uint32_t(data_[pos_ + 3]) << 24;
    pos_ += 4;
    return true;
  }
  std::string Str(uint32_t id) const {
    auto found = strings_.find(id);
    return found == strings_.end() ? std::string() : found->second;
  }

  bool Node(AdmNode& node, int depth) {
    if (depth > 64) return false;
    uint32_t name = 0, count = 0;
    if (!U32(name) || !U32(count)) return false;
    node.name = Str(name);
    for (uint32_t i = 0; i < count; ++i) {
      AdmNode::Property property;
      uint32_t key = 0;
      if (!U32(key) || !U32(property.type)) return false;
      property.key = Str(key);
      const size_t size = property.type == 3 || property.type == 7 ? 8 : 4;
      if (property.type < 1 || property.type > 8) {
        error_ = "unknown property type " + std::to_string(property.type);
        return false;
      }
      if (data_.size() - pos_ < size) return false;
      uint64_t raw = 0;
      for (size_t b = 0; b < size; ++b) raw |= uint64_t(data_[pos_ + b]) << (8 * b);
      pos_ += size;
      switch (property.type) {
        case 1: property.value = int64_t(int32_t(uint32_t(raw))); break;
        case 2: {
          float f;
          const uint32_t bits = uint32_t(raw);
          std::memcpy(&f, &bits, 4);
          property.value = double(f);
          break;
        }
        case 3: {
          double d;
          std::memcpy(&d, &raw, 8);
          property.value = d;
          break;
        }
        case 5:
        case 8: property.value = Str(uint32_t(raw)); break;
        case 7: property.value = int64_t(raw); break;
        default: property.value = int64_t(uint32_t(raw)); break;  // 4 uint32, 6 bool
      }
      node.properties.push_back(std::move(property));
    }
    if (!U32(count)) return false;
    node.children.resize(count);
    for (auto& child : node.children) {
      if (!Node(child, depth + 1)) return false;
    }
    return true;
  }

  const std::vector<uint8_t>& data_;
  size_t pos_ = 0;
  std::map<uint32_t, std::string> strings_;
  std::string error_;
};

}  // namespace

bool ParseAdm(const std::vector<uint8_t>& data, AdmNode& root, std::string& error) {
  return AdmReader(data).Root(root, error);
}

struct Pak::State {
  std::ifstream file;
  mz_zip_archive zip{};
  bool open = false;
};

namespace {

size_t ReadAt(void* opaque, mz_uint64 offset, void* buffer, size_t size) {
  auto& file = *static_cast<std::ifstream*>(opaque);
  file.clear();
  file.seekg(static_cast<std::streamoff>(offset));
  file.read(static_cast<char*>(buffer), static_cast<std::streamsize>(size));
  return static_cast<size_t>(file.gcount());
}

}  // namespace

std::unique_ptr<Pak> Pak::Open(const std::filesystem::path& path, std::string& error) {
  auto pak = std::unique_ptr<Pak>(new Pak());
  pak->state_ = std::make_unique<State>();
  State& state = *pak->state_;
  std::error_code ec;
  const auto size = std::filesystem::file_size(path, ec);
  state.file.open(path, std::ios::binary);
  if (ec || !state.file) {
    error = "it cannot be opened";
    return nullptr;
  }
  mz_zip_zero_struct(&state.zip);
  state.zip.m_pRead = ReadAt;
  state.zip.m_pIO_opaque = &state.file;
  if (!mz_zip_reader_init(&state.zip, size, 0)) {
    error = mz_zip_get_error_string(mz_zip_get_last_error(&state.zip));
    return nullptr;
  }
  state.open = true;
  return pak;
}

Pak::~Pak() {
  if (state_ && state_->open) mz_zip_reader_end(&state_->zip);
}

void Pak::ForEach(const std::function<bool(uint32_t, const std::string&)>& visit) const {
  auto& zip = state_->zip;
  const mz_uint count = mz_zip_reader_get_num_files(&zip);
  for (mz_uint i = 0; i < count; ++i) {
    mz_zip_archive_file_stat stat;
    if (!mz_zip_reader_file_stat(&zip, i, &stat) || stat.m_is_directory) continue;
    std::string name = stat.m_filename;
    for (char& c : name) {
      if (c == '\\') c = '/';
      else if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    if (!visit(i, name)) return;
  }
}

bool Pak::Read(uint32_t index, std::vector<uint8_t>& out, std::string& error) const {
  auto& zip = state_->zip;
  mz_zip_archive_file_stat stat;
  if (!mz_zip_reader_file_stat(&zip, index, &stat) || stat.m_uncomp_size > (1u << 30)) {
    error = "bad entry";
    return false;
  }
  out.assign(static_cast<size_t>(stat.m_uncomp_size), 0);
  if (!mz_zip_reader_extract_to_mem(&zip, index, out.data(), out.size(), 0)) {
    error = mz_zip_get_error_string(mz_zip_get_last_error(&zip));
    out.clear();
    return false;
  }
  return true;
}

}  // namespace torchlight::save_import
