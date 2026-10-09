#include "mods/unit_index.h"

#include <algorithm>
#include <map>
#include <unordered_set>
#include <utility>

namespace torchlight::mods {

namespace {

class Reader {
 public:
  explicit Reader(std::span<const uint8_t> bytes) : bytes_(bytes) {}
  bool Need(size_t n) const { return bytes_.size() - at_ >= n; }
  bool U8(uint8_t& v) {
    if (!Need(1)) return false;
    v = bytes_[at_++];
    return true;
  }
  bool U16(uint16_t& v) {
    if (!Need(2)) return false;
    v = static_cast<uint16_t>(bytes_[at_] | bytes_[at_ + 1] << 8);
    at_ += 2;
    return true;
  }
  bool U32(uint32_t& v) {
    if (!Need(4)) return false;
    v = 0;
    for (int i = 3; i >= 0; --i) v = v << 8 | bytes_[at_ + i];
    at_ += 4;
    return true;
  }
  bool I64(int64_t& v) {
    if (!Need(8)) return false;
    uint64_t u = 0;
    for (int i = 7; i >= 0; --i) u = u << 8 | bytes_[at_ + i];
    at_ += 8;
    v = static_cast<int64_t>(u);
    return true;
  }
  bool Text(std::u16string& s) {
    uint16_t n = 0;
    if (!U16(n) || !Need(2 * size_t{n})) return false;
    s.resize(n);
    for (uint16_t i = 0; i < n; ++i) {
      uint16_t c = 0;
      U16(c);
      s[i] = static_cast<char16_t>(c);
    }
    return true;
  }
  size_t at() const { return at_; }
  bool done() const { return at_ == bytes_.size(); }

 private:
  std::span<const uint8_t> bytes_;
  size_t at_ = 0;
};

void PutU16(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(static_cast<uint8_t>(v));
  out.push_back(static_cast<uint8_t>(v >> 8));
}
void PutU32(std::vector<uint8_t>& out, uint32_t v) {
  for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
void PutText(std::vector<uint8_t>& out, const std::u16string& s) {
  PutU16(out, static_cast<uint32_t>(std::min<size_t>(s.size(), 0xFFFF)));
  for (size_t i = 0; i < s.size() && i < 0xFFFF; ++i) PutU16(out, s[i]);
}

std::string Narrow(const std::u16string& s) {
  std::string out;
  for (char16_t c : s) out.push_back(c < 0x80 ? static_cast<char>(c) : '?');
  return out;
}

}  // namespace

size_t UnitIndex::size() const {
  size_t n = 0;
  for (const auto& g : groups) n += g.size();
  return n;
}

std::optional<UnitIndex> ParseUnitIndex(std::span<const uint8_t> bytes, std::string* error) {
  Reader r(bytes);
  UnitIndex index;
  auto fail = [&](const char* what) -> std::optional<UnitIndex> {
    if (error) *error = std::string(what) + " at byte " + std::to_string(r.at());
    return std::nullopt;
  };
  for (auto& group : index.groups) {
    uint32_t count = 0;
    if (!r.U32(count)) return fail("truncated group count");
    if (count > bytes.size() / 33) return fail("implausible group count");  // an entry is >= 33 bytes
    group.resize(count);
    for (UnitEntry& e : group) {
      uint8_t equipment = 0;
      if (!r.I64(e.guid) || !r.Text(e.name) || !r.Text(e.file) || !r.U8(equipment) ||
          !r.U32(e.level) || !r.U32(e.min_level) || !r.U32(e.max_level) || !r.U32(e.rarity) ||
          !r.U32(e.rarity_hardcore) || !r.Text(e.unit_type)) {
        return fail("truncated entry");
      }
      e.equipment = equipment != 0;
    }
  }
  if (!r.done()) return fail("bytes left after the last group");
  return index;
}

std::vector<uint8_t> WriteUnitIndex(const UnitIndex& index) {
  std::vector<uint8_t> out;
  for (const auto& group : index.groups) {
    PutU32(out, static_cast<uint32_t>(group.size()));
    for (const UnitEntry& e : group) {
      const uint64_t guid = static_cast<uint64_t>(e.guid);
      PutU32(out, static_cast<uint32_t>(guid));
      PutU32(out, static_cast<uint32_t>(guid >> 32));
      PutText(out, e.name);
      PutText(out, e.file);
      out.push_back(e.equipment ? 1 : 0);
      PutU32(out, e.level);
      PutU32(out, e.min_level);
      PutU32(out, e.max_level);
      PutU32(out, e.rarity);
      PutU32(out, e.rarity_hardcore);
      PutText(out, e.unit_type);
    }
  }
  return out;
}

std::u16string NormalizeUnitFile(std::u16string_view path) {
  std::u16string out(path);
  for (char16_t& c : out) {
    if (c == u'\\') c = u'/';
    else if (c >= u'a' && c <= u'z') c = static_cast<char16_t>(c - u'a' + u'A');
  }
  return out;
}

std::optional<UnitGroup> GroupOfUnitFile(std::u16string_view path) {
  const std::u16string p = NormalizeUnitFile(path);
  static constexpr std::pair<std::u16string_view, UnitGroup> kGroups[] = {
      {u"MEDIA/UNITS/ITEMS/", UnitGroup::kItems},
      {u"MEDIA/UNITS/MONSTERS/", UnitGroup::kMonsters},
      {u"MEDIA/UNITS/PLAYERS/", UnitGroup::kPlayers},
      {u"MEDIA/UNITS/PROPS/", UnitGroup::kProps},
  };
  for (const auto& [prefix, group] : kGroups) {
    if (p.size() > prefix.size() && p.compare(0, prefix.size(), prefix) == 0) return group;
  }
  return std::nullopt;
}

std::vector<std::string> CompareUnitIndexes(const UnitIndex& expected, const UnitIndex& actual,
                                            size_t limit) {
  std::vector<std::string> lines;
  size_t more = 0;
  auto add = [&](std::string line) {
    if (lines.size() < limit) lines.push_back(std::move(line));
    else ++more;
  };
  for (size_t g = 0; g < kUnitGroupCount; ++g) {
    std::map<int64_t, const UnitEntry*> want, got;
    for (const UnitEntry& e : expected.groups[g]) want[e.guid] = &e;
    for (const UnitEntry& e : actual.groups[g]) got[e.guid] = &e;
    const std::string where = "group " + std::to_string(g) + ", ";
    for (const auto& [guid, e] : want) {
      auto it = got.find(guid);
      if (it == got.end()) {
        add(where + "missing " + Narrow(e->file));
        continue;
      }
      const UnitEntry& a = *it->second;
      auto field = [&](const char* name, auto x, auto y) {
        if (!(x == y)) add(where + Narrow(e->file) + ": " + name + " differs");
      };
      field("NAME", e->name, a.name);
      field("file", NormalizeUnitFile(e->file), NormalizeUnitFile(a.file));
      field("EQUIPMENT", e->equipment, a.equipment);
      field("LEVEL", e->level, a.level);
      field("MINLEVEL", e->min_level, a.min_level);
      field("MAXLEVEL", e->max_level, a.max_level);
      field("RARITY", e->rarity, a.rarity);
      field("RARITY_HARDCORE", e->rarity_hardcore, a.rarity_hardcore);
      field("UNITTYPE", e->unit_type, a.unit_type);
    }
    for (const auto& [guid, a] : got) {
      if (!want.count(guid)) add(where + "extra " + Narrow(a->file));
    }
  }
  if (more) lines.push_back("... and " + std::to_string(more) + " more");
  return lines;
}

UnitIndex MergeUnitIndex(const UnitIndex& base, const std::vector<UnitEntry>& units,
                         std::vector<std::u16string>* skipped) {
  UnitIndex out = base;
  for (const UnitEntry& unit : units) {
    const auto group = GroupOfUnitFile(unit.file);
    if (!group) {
      if (skipped) skipped->push_back(unit.file);
      continue;
    }
    const std::u16string file = NormalizeUnitFile(unit.file);
    // Same file: takes that entry's place (in whatever group it was).
    bool placed = false;
    for (auto& g : out.groups) {
      for (UnitEntry& e : g) {
        if (NormalizeUnitFile(e.file) == file) {
          if (&g == &out.groups[static_cast<size_t>(*group)] && !placed) {
            e = unit;
            placed = true;
          } else {
            e.guid = 0, e.file.clear();  // marked, removed below
          }
        }
      }
    }
    // Same GUID under another file: the later (higher priority) one wins.
    for (auto& g : out.groups) {
      for (UnitEntry& e : g) {
        if (e.guid == unit.guid && !(placed && NormalizeUnitFile(e.file) == file)) {
          e.guid = 0, e.file.clear();
        }
      }
    }
    for (auto& g : out.groups) {
      std::erase_if(g, [](const UnitEntry& e) { return e.file.empty(); });
    }
    if (!placed) out.groups[static_cast<size_t>(*group)].push_back(unit);
  }
  return out;
}

size_t UniqueUnitGuids(const UnitIndex& index) {
  std::unordered_set<int64_t> guids;
  for (const auto& group : index.groups) {
    for (const UnitEntry& e : group) guids.insert(e.guid);
  }
  return guids.size();
}

LoadedIndex CheckLoadedIndex(size_t loaded, size_t expected) {
  if (loaded == expected) return LoadedIndex::kComplete;
  if (loaded == 0) return LoadedIndex::kEmpty;
  return loaded < expected ? LoadedIndex::kIncomplete : LoadedIndex::kMore;
}

}  // namespace torchlight::mods
