// The game's text data format (docs/mods.md, section 7, reading 3): blocks "[NAME]" ... "[/NAME]"
// holding nested blocks and value lines "<TYPE>KEY:VALUE" (types by name: INTEGER, FLOAT, STRING,
// BOOL, ...; the guest's sub_82393EA0, PC's block formats "[%s]" and "[/%s]"). Used for a mod's
// mod.dat and for mods.dat. No guest, OGRE or platform types.
//
// The encoding of real files is to be confirmed with a real mod (docs/mods.md, section 10); the
// reader accepts UTF-16LE with a byte order mark, UTF-8 and ASCII. The writer emits UTF-16LE with a
// byte order mark, the game's wide-character text.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace torchlight::mods {

struct DatValue {
  std::string type;   // as written, upper case (STRING, INTEGER, BOOL, ...)
  std::string key;    // as written
  std::string value;  // UTF-8
};

struct DatBlock {
  std::string name;
  std::vector<DatValue> values;
  std::vector<DatBlock> children;

  // The first value with this key (case-insensitive), or nothing.
  const DatValue* Find(std::string_view key) const;
};

// The top-level blocks of a file, or nothing if a line is neither a block tag nor a value line, or a
// value is outside any block. `error` says where. As tolerant as the game is with PC mods' files:
// a line ends at LF, CR LF or a lone CR; "//" comment lines are skipped; a closing tag that matches
// no open block is skipped, one that matches an outer block closes the inner ones; blocks still
// open at the end are closed there.
std::optional<std::vector<DatBlock>> ParseDatText(const std::vector<uint8_t>& bytes, std::string* error);

// The value of the first value line "<TYPE>KEY:VALUE" with this key (case-insensitive) anywhere in
// the file, whatever the rest of the file is: for a file ParseDatText refuses (a PC mod's typo, such
// as "cryptic[UNIT]" on the first line of some of Enhanced Edition's items), whose single value
// is still needed (its UNIT_GUID). None without one.
std::optional<std::string> FindDatValueAnywhere(const std::vector<uint8_t>& bytes, std::string_view key);

// The file's bytes for these blocks (UTF-16LE with a byte order mark, tab indentation, CRLF).
std::vector<uint8_t> WriteDatText(const std::vector<DatBlock>& blocks);

}  // namespace torchlight::mods
