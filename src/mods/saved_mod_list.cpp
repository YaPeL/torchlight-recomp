#include "mods/saved_mod_list.h"

#include <algorithm>
#include <functional>
#include <set>
#include <utility>

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

std::optional<std::vector<uint8_t>> RepairModListAtLoad(
    std::span<const uint8_t> body, const std::vector<std::u16string>& candidates,
    const std::function<bool(std::span<const uint8_t>)>& reads, std::string* why) {
  // The candidates' units, big-endian, without repeats or empty names.
  std::vector<std::vector<uint8_t>> names;
  {
    std::set<std::u16string> seen;
    for (const std::u16string& c : candidates) {
      if (c.empty() || c.size() > 0xFFFF || !seen.insert(c).second) continue;
      std::vector<uint8_t> bytes;
      for (char16_t u : c) PutU16(bytes, u);
      names.push_back(std::move(bytes));
    }
  }
  constexpr uint32_t kMaxNames = 256;
  constexpr size_t kMaxSplits = 16;
  // A split: per name, where its length goes and the length (in units).
  using Split = std::vector<std::pair<size_t, size_t>>;
  std::vector<Split> splits;
  Split current;
  // Matches `left` names from `at` (each: a 0 length, then one candidate's units).
  std::function<void(size_t, uint32_t)> match = [&](size_t at, uint32_t left) {
    if (splits.size() >= kMaxSplits) return;
    if (left == 0) {
      splits.push_back(current);
      return;
    }
    if (at + 2 > body.size() || body[at] != 0 || body[at + 1] != 0) return;
    for (const auto& name : names) {
      if (at + 2 + name.size() > body.size()) continue;
      if (!std::equal(name.begin(), name.end(), body.begin() + static_cast<std::ptrdiff_t>(at + 2))) continue;
      current.emplace_back(at, name.size() / 2);
      match(at + 2 + name.size(), left - 1);
      current.pop_back();
    }
  };
  for (size_t p = 0; p + 6 <= body.size(); ++p) {
    const uint32_t count = uint32_t(body[p]) << 24 | uint32_t(body[p + 1]) << 16 | uint32_t(body[p + 2]) << 8 |
                           uint32_t(body[p + 3]);
    if (count == 0 || count > kMaxNames || body[p + 4] != 0 || body[p + 5] != 0) continue;
    match(p + 4, count);
  }
  std::vector<std::vector<uint8_t>> repaired;
  for (const Split& split : splits) {
    std::vector<uint8_t> copy(body.begin(), body.end());
    for (const auto& [at, length] : split) {
      copy[at] = uint8_t(length >> 8);
      copy[at + 1] = uint8_t(length);
    }
    if (reads(copy) && std::find(repaired.begin(), repaired.end(), copy) == repaired.end()) {
      repaired.push_back(std::move(copy));
    }
  }
  if (repaired.size() == 1) return std::move(repaired.front());
  if (why) {
    *why = splits.empty()     ? "no list of known mod names with empty lengths in it"
           : repaired.empty() ? "no repair of the list makes the save read"
                              : "more than one repair of the list makes the save read";
  }
  return std::nullopt;
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
