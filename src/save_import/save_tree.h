// Reading a save body into a tree of values and writing a tree back, driven by the schema; and the
// two file framings (PC: body + u32 total length; Xbox 360: body + SHA-256 of the body; the shared
// stash has neither). The port of tools/save_convert/savefile.py: parsing and writing an unchanged
// save gives back the same bytes, and writing recomputes the end-of-block offsets.

#pragma once

#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "save_import/schema.h"

namespace torchlight::save_import {

// The only save version the converter is verified with as input (Torchlight PC v1.15).
inline constexpr uint32_t kPcVersion = 23;

enum class Endian { kLittle, kBig };

// A value of the tree: a number (raw bits; f32 too), text (UTF-16 code units), an element (named
// fields, in schema order) or a list (of numbers, texts, elements or lists). Nodes never move once
// built (std::list), so references to them stay valid.
struct Node {
  enum class Kind { kNumber, kText, kElement, kList };
  Kind kind = Kind::kNumber;
  uint64_t number = 0;
  std::u16string text;
  std::list<std::pair<std::string, Node>> fields;
  std::list<Node> items;

  Node* Find(std::string_view name);
  const Node* Find(std::string_view name) const;
};

// A field with a role (GUID or name reference), found while parsing.
struct Ref {
  std::string role;
  size_t offset = 0;
  std::string path;         // e.g. "player/items/item/unit_guid"
  Node* value = nullptr;    // the field's node, to read or replace
  Node* element = nullptr;  // the element that holds it (null for an item that is one field)
  std::string only_if_set;
};

struct Parsed {
  Node tree;
  std::vector<Ref> refs;
  std::optional<uint32_t> version;
};

// A save that cannot be read or converted; the message is meant for the user.
struct SaveError {
  std::string message;
};

using Bytes = std::vector<uint8_t>;

// Parses a body (without its trailer) with the schema struct `root`; every byte must be consumed.
// Returns null and sets `error` otherwise.
std::unique_ptr<Parsed> ParseBody(const Schema& schema, const Struct& root,
                                  std::span<const uint8_t> body, Endian endian, SaveError& error);
Bytes WriteBody(const Schema& schema, const Struct& root, const Node& tree, Endian endian);

// Body of a PC N.SVT after checking its length trailer; of an Xbox 360 N.TSV after checking its
// SHA-256. False with `error` set otherwise.
bool SplitPc(std::span<const uint8_t> data, std::span<const uint8_t>& body, SaveError& error);
bool Split360(std::span<const uint8_t> data, std::span<const uint8_t>& body, SaveError& error);

// A PC character save (version 23 only), parsed.
std::unique_ptr<Parsed> ReadPc(const Schema& schema, std::span<const uint8_t> data, SaveError& error);
// The 360 N.TSV of a parsed PC save (its tree possibly changed: GUID replacements, adaptations).
Bytes Write360(const Schema& schema, const Parsed& pc);

// Shared stash (no trailer on either platform): PC (version 23 only) and 360.
std::unique_ptr<Parsed> ReadPcStash(const Schema& schema, std::span<const uint8_t> data,
                                    SaveError& error);
std::unique_ptr<Parsed> Read360Stash(const Schema& schema, std::span<const uint8_t> data,
                                     SaveError& error);
Bytes Write360Stash(const Schema& schema, const Parsed& pc);

// A u64 GUID as the game's signed value (-1 and 0 mean "none").
inline int64_t Signed64(uint64_t value) { return static_cast<int64_t>(value); }

}  // namespace torchlight::save_import
