#include "save_import/save_tree.h"

#include <algorithm>
#include <cstring>

#include "save_import/digest.h"

namespace torchlight::save_import {
namespace {

constexpr size_t kSha256Size = 32;
constexpr size_t kLengthTrailerSize = 4;

size_t SizeOf(FieldType type) {
  switch (type) {
    case FieldType::kU8: return 1;
    case FieldType::kU16: return 2;
    case FieldType::kU32: return 4;
    case FieldType::kU64: return 8;
    case FieldType::kF32: return 4;
    default: return 0;
  }
}

bool IsNumber(FieldType type) { return SizeOf(type) != 0; }

bool Holds(const Field& condition, std::optional<uint32_t> version) {
  const uint32_t v = version.value_or(0);
  switch (condition.test) {
    case Field::Test::kGreaterOrEqual: return v >= condition.version;
    case Field::Test::kLess: return v < condition.version;
    case Field::Test::kEqual: return v == condition.version;
  }
  return false;
}

std::string Join(const std::vector<std::string>& path) {
  std::string out;
  for (const auto& part : path) {
    if (!out.empty()) out += '/';
    out += part;
  }
  return out;
}

class Reader {
 public:
  Reader(std::span<const uint8_t> data, Endian endian, Parsed& out)
      : data_(data), endian_(endian), out_(out) {}

  void Element(const std::vector<Field>& fields, Node& element) {
    element.kind = Node::Kind::kElement;
    std::optional<std::pair<uint64_t, size_t>> end_check;
    Fields(fields, element, end_check);
    if (end_check && end_check->first != offset_) {
      throw SaveError{"inconsistent end-of-block offset at " + Join(path_) + ": it says " +
                      std::to_string(end_check->first) + ", the block ends at " +
                      std::to_string(offset_)};
    }
  }

  size_t offset() const { return offset_; }

 private:
  std::string Where() const {
    return (path_.empty() ? std::string("<root>") : Join(path_)) + " (offset " +
           std::to_string(offset_) + ")";
  }

  void Need(size_t size) {
    if (size > data_.size() - offset_) {
      throw SaveError{"the file ends earlier than expected at " + Where()};
    }
  }

  uint64_t Number(FieldType type) {
    const size_t size = SizeOf(type);
    Need(size);
    uint64_t value = 0;
    for (size_t i = 0; i < size; ++i) {
      const uint8_t byte = data_[offset_ + i];
      if (endian_ == Endian::kBig) value = (value << 8) | byte;
      else value |= uint64_t(byte) << (8 * i);
    }
    offset_ += size;
    return value;
  }

  std::u16string Text(FieldType count_type) {
    const uint64_t count = Number(count_type);
    if (count > (data_.size() - offset_) / 2) Need(data_.size() - offset_ + 1);
    std::u16string text;
    text.reserve(count);
    for (uint64_t i = 0; i < count; ++i) text.push_back(static_cast<char16_t>(Number(FieldType::kU16)));
    return text;
  }

  void Fields(const std::vector<Field>& fields, Node& element,
              std::optional<std::pair<uint64_t, size_t>>& end_check) {
    for (const Field& field : fields) {
      if (field.type == FieldType::kCondition) {
        Fields(Holds(field, out_.version) ? field.then_fields : field.else_fields, element, end_check);
        continue;
      }
      element.fields.emplace_back(field.name, Node{});
      Node& value = element.fields.back().second;
      path_.push_back(field.name);
      const size_t start = offset_;
      Read(field, &element, value);
      if (field.end_of_element) end_check = std::make_pair(value.number, start);
      path_.pop_back();
    }
  }

  // `element` holds the field (null for an item that is a single field: there is no element).
  void Read(const Field& field, Node* element, Node& value) {
    const size_t start = offset_;
    if (IsNumber(field.type)) {
      if (field.fixed_count >= 0 || !field.count_product.empty()) {
        uint64_t count = field.fixed_count >= 0 ? uint64_t(field.fixed_count) : 1;
        for (const auto& name : field.count_product) {
          const Node* factor = element ? element->Find(name) : nullptr;
          count *= factor ? factor->number : 0;
        }
        if (count > (data_.size() - offset_) / SizeOf(field.type)) Need(data_.size() - offset_ + 1);
        value.kind = Node::Kind::kList;
        for (uint64_t i = 0; i < count; ++i) {
          value.items.emplace_back();
          value.items.back().number = Number(field.type);
        }
        return;
      }
      value.kind = Node::Kind::kNumber;
      value.number = Number(field.type);
      if (field.role == "version") {
        out_.version = static_cast<uint32_t>(value.number);
      } else if (!field.role.empty()) {
        out_.refs.push_back({field.role, start, Join(path_), &value, element, field.only_if_set});
      }
      return;
    }
    switch (field.type) {
      case FieldType::kWString16:
      case FieldType::kWString32:
        value.kind = Node::Kind::kText;
        value.text = Text(field.type == FieldType::kWString16 ? FieldType::kU16 : FieldType::kU32);
        if (!field.role.empty()) {
          out_.refs.push_back({field.role, start, Join(path_), &value, element, field.only_if_set});
        }
        return;
      case FieldType::kStruct:
        Element(field.target->fields, value);
        return;
      case FieldType::kRepeat:
        value.kind = Node::Kind::kList;
        for (int i = 0; i < field.times; ++i) Item(field, value);
        return;
      case FieldType::kList: {
        value.kind = Node::Kind::kList;
        const uint64_t count = Number(field.count_type);
        if (count > data_.size() - offset_) {
          throw SaveError{"the list " + Where() + " claims " + std::to_string(count) +
                          " elements, more than the bytes left"};
        }
        for (uint64_t i = 0; i < count; ++i) Item(field, value);
        return;
      }
      default:
        throw SaveError{"internal error: unknown field type at " + Where()};
    }
  }

  void Item(const Field& field, Node& list) {
    list.items.emplace_back();
    Node& item = list.items.back();
    if (field.item_is_element) {
      Element(field.item_fields, item);
      return;
    }
    // A single field: its value is the item.
    path_.push_back(field.item->name);
    Read(*field.item, nullptr, item);
    path_.pop_back();
  }

  std::span<const uint8_t> data_;
  Endian endian_;
  Parsed& out_;
  size_t offset_ = 0;
  std::vector<std::string> path_;
};

class Writer {
 public:
  explicit Writer(Endian endian) : endian_(endian) {}

  void Element(const std::vector<Field>& fields, const Node& element) {
    std::vector<size_t> saved;
    saved.swap(pending_end_);
    Fields(fields, element);
    for (size_t at : pending_end_) Patch(at, out_.size());
    pending_end_.swap(saved);
  }

  Bytes Take() { return std::move(out_); }

 private:
  void Number(FieldType type, uint64_t value) {
    const size_t size = SizeOf(type);
    for (size_t i = 0; i < size; ++i) {
      const size_t shift = endian_ == Endian::kBig ? 8 * (size - 1 - i) : 8 * i;
      out_.push_back(static_cast<uint8_t>(value >> shift));
    }
  }
  void Patch(size_t at, size_t value) {
    for (size_t i = 0; i < 4; ++i) {
      const size_t shift = endian_ == Endian::kBig ? 8 * (3 - i) : 8 * i;
      out_[at + i] = static_cast<uint8_t>(value >> shift);
    }
  }

  void Fields(const std::vector<Field>& fields, const Node& element) {
    for (const Field& field : fields) {
      if (field.type == FieldType::kCondition) {
        Fields(Holds(field, version_) ? field.then_fields : field.else_fields, element);
        continue;
      }
      const Node* value = element.Find(field.name);
      if (!value) throw SaveError{"internal error: no value for " + field.name};
      Write(field, *value);
    }
  }

  void Write(const Field& field, const Node& value) {
    if (IsNumber(field.type)) {
      if (field.fixed_count >= 0 || !field.count_product.empty()) {
        for (const Node& item : value.items) Number(field.type, item.number);
        return;
      }
      if (field.end_of_element) {
        pending_end_.push_back(out_.size());
        Number(field.type, 0);  // patched when the element ends
        return;
      }
      if (field.role == "version") version_ = static_cast<uint32_t>(value.number);
      Number(field.type, value.number);
      return;
    }
    switch (field.type) {
      case FieldType::kWString16:
      case FieldType::kWString32:
        Number(field.type == FieldType::kWString16 ? FieldType::kU16 : FieldType::kU32,
               value.text.size());
        for (char16_t unit : value.text) Number(FieldType::kU16, unit);
        return;
      case FieldType::kStruct:
        Element(field.target->fields, value);
        return;
      case FieldType::kRepeat:
        if (value.items.size() != size_t(field.times)) {
          throw SaveError{"internal error: " + field.name + " must have " +
                          std::to_string(field.times) + " entries"};
        }
        for (const Node& item : value.items) Item(field, item);
        return;
      case FieldType::kList:
        Number(field.count_type, value.items.size());
        for (const Node& item : value.items) Item(field, item);
        return;
      default:
        throw SaveError{"internal error: unknown field type"};
    }
  }

  void Item(const Field& field, const Node& item) {
    if (field.item_is_element) Element(field.item_fields, item);
    else Write(*field.item, item);
  }

  Endian endian_;
  Bytes out_;
  std::optional<uint32_t> version_;
  std::vector<size_t> pending_end_;
};

uint32_t LoadLe32(std::span<const uint8_t> data, size_t at) {
  return uint32_t(data[at]) | uint32_t(data[at + 1]) << 8 | uint32_t(data[at + 2]) << 16 |
         uint32_t(data[at + 3]) << 24;
}

std::unique_ptr<Parsed> ReadPcVersioned(const Schema& schema, const Struct& root,
                                        std::span<const uint8_t> body, const char* what,
                                        SaveError& error) {
  const uint32_t version = LoadLe32(body, 0);
  if (version != kPcVersion) {
    error.message = std::string("unsupported ") + what + " version " + std::to_string(version) +
                    ": the converter is only verified with version " +
                    std::to_string(kPcVersion) + " (Torchlight PC v1.15)";
    return nullptr;
  }
  return ParseBody(schema, root, body, Endian::kLittle, error);
}

}  // namespace

Node* Node::Find(std::string_view name) {
  for (auto& [key, value] : fields) {
    if (key == name) return &value;
  }
  return nullptr;
}

const Node* Node::Find(std::string_view name) const {
  return const_cast<Node*>(this)->Find(name);
}

std::unique_ptr<Parsed> ParseBody(const Schema& schema, const Struct& root,
                                  std::span<const uint8_t> body, Endian endian, SaveError& error) {
  (void)schema;
  auto parsed = std::make_unique<Parsed>();
  try {
    Reader reader(body, endian, *parsed);
    reader.Element(root.fields, parsed->tree);
    if (reader.offset() != body.size()) {
      error.message = std::to_string(body.size() - reader.offset()) +
                      " bytes left uninterpreted after the save (offset " +
                      std::to_string(reader.offset()) + " of " + std::to_string(body.size()) + ")";
      return nullptr;
    }
  } catch (const SaveError& e) {
    error = e;
    return nullptr;
  }
  return parsed;
}

Bytes WriteBody(const Schema& schema, const Struct& root, const Node& tree, Endian endian) {
  (void)schema;
  Writer writer(endian);
  writer.Element(root.fields, tree);
  return writer.Take();
}

bool SplitPc(std::span<const uint8_t> data, std::span<const uint8_t>& body, SaveError& error) {
  if (data.size() < kLengthTrailerSize + 4) {
    error.message = "the file is too small to be a PC save (.SVT)";
    return false;
  }
  const uint32_t stated = LoadLe32(data, data.size() - kLengthTrailerSize);
  if (stated != data.size()) {
    error.message = "the length stored at the end (" + std::to_string(stated) +
                    ") does not match the file size (" + std::to_string(data.size()) +
                    "): it is not a PC .SVT or it is truncated";
    return false;
  }
  body = data.first(data.size() - kLengthTrailerSize);
  return true;
}

bool Split360(std::span<const uint8_t> data, std::span<const uint8_t>& body, SaveError& error) {
  if (data.size() < kSha256Size + 4) {
    error.message = "the file is too small to be a 360 save (.TSV)";
    return false;
  }
  body = data.first(data.size() - kSha256Size);
  const Sha256Digest digest = Sha256(body);
  if (!std::equal(digest.begin(), digest.end(), data.end() - kSha256Size)) {
    error.message = "the trailing SHA-256 does not match: it is not a .TSV or it is damaged";
    return false;
  }
  return true;
}

std::unique_ptr<Parsed> ReadPc(const Schema& schema, std::span<const uint8_t> data,
                               SaveError& error) {
  std::span<const uint8_t> body;
  if (!SplitPc(data, body, error)) return nullptr;
  return ReadPcVersioned(schema, *schema.root(), body, "save", error);
}

Bytes Write360(const Schema& schema, const Parsed& pc) {
  Bytes out = WriteBody(schema, *schema.root(), pc.tree, Endian::kBig);
  const Sha256Digest digest = Sha256(out);
  out.insert(out.end(), digest.begin(), digest.end());
  return out;
}

std::unique_ptr<Parsed> ReadPcStash(const Schema& schema, std::span<const uint8_t> data,
                                    SaveError& error) {
  if (data.size() < 8) {
    error.message = "the file is too small to be a shared stash";
    return nullptr;
  }
  return ReadPcVersioned(schema, *schema.stash_root(), data, "stash", error);
}

std::unique_ptr<Parsed> Read360Stash(const Schema& schema, std::span<const uint8_t> data,
                                     SaveError& error) {
  if (data.size() < 8) {
    error.message = "the file is too small to be a shared stash";
    return nullptr;
  }
  return ParseBody(schema, *schema.stash_root(), data, Endian::kBig, error);
}

Bytes Write360Stash(const Schema& schema, const Parsed& pc) {
  return WriteBody(schema, *schema.stash_root(), pc.tree, Endian::kBig);
}

}  // namespace torchlight::save_import
