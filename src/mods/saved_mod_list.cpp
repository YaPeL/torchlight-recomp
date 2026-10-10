#include "mods/saved_mod_list.h"

#include <algorithm>

namespace torchlight::mods {

namespace {

void PutU16(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(static_cast<uint8_t>(v >> 8));
  out.push_back(static_cast<uint8_t>(v));
}

void PutU32(std::vector<uint8_t>& out, uint32_t v) {
  PutU16(out, v >> 16);
  PutU16(out, v & 0xFFFF);
}

}  // namespace

std::vector<uint8_t> EncodeSavedModList(uint32_t before, const std::vector<std::u16string>& names,
                                        bool runic_bug) {
  std::vector<uint8_t> out;
  PutU32(out, before);
  PutU32(out, static_cast<uint32_t>(names.size()));
  for (const std::u16string& name : names) {
    PutU16(out, runic_bug ? 0 : static_cast<uint32_t>(name.size()));
    for (char16_t c : name) PutU16(out, c);
  }
  return out;
}

ModListRepair RepairSavedModList(std::span<uint8_t> written, uint32_t before,
                                 const std::vector<std::u16string>& names) {
  if (names.empty()) return ModListRepair::kNothing;
  for (const std::u16string& name : names) {
    if (name.size() > 0xFFFF) return ModListRepair::kTooLong;
  }
  // The whole list as the bug leaves it, anchored on the field before it and the exact count: a
  // match has to repeat every byte of it, not just look like one entry.
  const std::vector<uint8_t> pattern = EncodeSavedModList(before, names, /*runic_bug=*/true);
  size_t found = written.size();
  int matches = 0;
  for (auto it = written.begin();; ++it) {
    it = std::search(it, written.end(), pattern.begin(), pattern.end());
    if (it == written.end()) break;
    if (++matches > 1) return ModListRepair::kAmbiguous;
    found = static_cast<size_t>(it - written.begin());
  }
  if (matches == 0) return ModListRepair::kNotFound;
  size_t at = found + 8;  // past `before` and the count
  for (const std::u16string& name : names) {
    written[at] = static_cast<uint8_t>(name.size() >> 8);
    written[at + 1] = static_cast<uint8_t>(name.size());
    at += 2 + 2 * name.size();
  }
  return ModListRepair::kRepaired;
}

const char* ToString(ModListRepair result) {
  switch (result) {
    case ModListRepair::kNothing: return "nothing to repair";
    case ModListRepair::kRepaired: return "repaired";
    case ModListRepair::kNotFound: return "not found";
    case ModListRepair::kAmbiguous: return "found more than once";
    case ModListRepair::kTooLong: return "a name is too long";
  }
  return "?";
}

}  // namespace torchlight::mods
