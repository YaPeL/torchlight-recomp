// The unit save writer's repair (save_mod_list.h) on synthetic guest memory, with a stand-in for
// the guest's writer that writes like it (guest_abi/mods.h kUnitSaveWriter): the u32 at +568, the
// count and each name with its length's high half (0), between other fields. Covers the repair,
// a buffer that moves while growing, the fallback (rewind, second call with the list emptied, the
// count restored, the stream's position and size as if written once) and units without names.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "game_menu/save_mod_list.h"
#include "guest_abi/mods.h"

namespace abi = torchlight::guest_abi;
using torchlight::game_menu::UnitSaveOutcome;
using torchlight::game_menu::WriteUnitSave;

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
  if (!ok) {
    ++failures;
    std::fprintf(stderr, "FAIL: %s\n", what);
  }
}

constexpr uint32_t kSaveData = 0x1000, kNames = 0x2000, kStream = 0x3000, kHeap = 0x4000;
constexpr uint32_t kBuffer = 0x10000, kMovedBuffer = 0x20000;
constexpr uint32_t kBefore = 0xFFFFFFFF;

uint64_t Read64(const uint8_t* base, uint32_t at) {
  return (uint64_t{abi::ReadU32(base, at)} << 32) | abi::ReadU32(base, at + 4);
}
void Write64(uint8_t* base, uint32_t at, uint64_t v) {
  abi::WriteU32(base, at, uint32_t(v >> 32));
  abi::WriteU32(base, at + 4, uint32_t(v));
}

struct Memory {
  std::vector<uint8_t> bytes = std::vector<uint8_t>(0x40000, 0xCD);
  uint8_t* base = bytes.data();

  void SetNames(const std::vector<std::u16string>& names) {
    for (size_t i = 0; i < names.size(); ++i) {
      // Up to 7 units inline (capacity 7), longer ones behind a pointer, as the guest's wstring.
      const uint32_t str = kNames + uint32_t(i) * 28;
      const bool inline_text = names[i].size() <= 7;
      const uint32_t text = inline_text ? str : kHeap + uint32_t(i) * 0x100;
      for (size_t c = 0; c < names[i].size(); ++c) {
        base[text + 2 * c] = uint8_t(names[i][c] >> 8);
        base[text + 2 * c + 1] = uint8_t(names[i][c]);
      }
      if (!inline_text) abi::WriteU32(base, str, text);
      abi::WriteU32(base, str + 16, uint32_t(names[i].size()));
      abi::WriteU32(base, str + 20, inline_text ? 7 : 31);
    }
    abi::WriteU32(base, kSaveData + 568, kBefore);
    abi::WriteU32(base, kSaveData + 576, kNames);
    abi::WriteU32(base, kSaveData + 580, uint32_t(names.size()));
  }
  void SetStream(uint64_t position) {
    abi::WriteU32(base, kStream, kBuffer);
    Write64(base, kStream + 16, position);
    Write64(base, kStream + 24, position);
    for (uint32_t i = 0; i < position; ++i) base[kBuffer + i] = uint8_t(0xA0 + i);  // earlier units
  }
};

// Writes like the guest's unit writer: a field, +568, the count, the names (length high half),
// another field; `twice` writes the list again in that last field (a stand-in for data that would
// make the list ambiguous); `move` moves the buffer first, as the stream's growth may.
struct FakeWriter {
  explicit FakeWriter(Memory& memory) : m(memory) {}
  Memory& m;
  bool twice = false, move = false;
  int calls = 0;
  std::vector<uint32_t> counts_seen;

  void Put(std::vector<uint8_t>& out, uint32_t v, int bytes) {
    for (int i = bytes - 1; i >= 0; --i) out.push_back(uint8_t(v >> (8 * i)));
  }
  void operator()() {
    ++calls;
    uint8_t* base = m.base;
    if (move) {
      const uint32_t old = abi::ReadU32(base, kStream);
      const uint64_t size = Read64(base, kStream + 24);
      for (uint64_t i = 0; i < size; ++i) base[kMovedBuffer + i] = base[old + i];
      abi::WriteU32(base, kStream, kMovedBuffer);
    }
    const auto names = abi::mods::ReadSavedModNames(base, kSaveData);
    counts_seen.push_back(abi::ReadU32(base, kSaveData + 580));
    std::vector<uint8_t> out;
    Put(out, 0x11223344, 4);
    auto list = [&] {
      Put(out, abi::ReadU32(base, kSaveData + 568), 4);
      Put(out, uint32_t(names.size()), 4);
      for (const auto& n : names) {
        const uint32_t length = uint32_t(n.size());
        Put(out, length >> 16, 2);  // the bug: the u32's first two bytes
        for (char16_t c : n) Put(out, c, 2);
      }
    };
    list();
    Put(out, 5, 4);
    if (twice) list();
    const uint32_t buffer = abi::ReadU32(base, kStream);
    const uint64_t position = Read64(base, kStream + 16);
    for (size_t i = 0; i < out.size(); ++i) base[buffer + position + i] = out[i];
    Write64(base, kStream + 16, position + out.size());
    if (position + out.size() > Read64(base, kStream + 24)) Write64(base, kStream + 24, position + out.size());
  }
};

std::vector<uint8_t> Expected(const std::vector<std::u16string>& names) {
  std::vector<uint8_t> out = {0x11, 0x22, 0x33, 0x44};
  const auto list = torchlight::mods::EncodeSavedModList(kBefore, names, false);
  out.insert(out.end(), list.begin(), list.end());
  out.insert(out.end(), {0, 0, 0, 5});
  return out;
}

bool StreamHolds(const Memory& m, uint32_t buffer, uint64_t start, const std::vector<uint8_t>& bytes) {
  if (Read64(m.base, kStream + 16) != start + bytes.size()) return false;
  if (Read64(m.base, kStream + 24) != start + bytes.size()) return false;
  for (uint64_t i = 0; i < start; ++i) {
    if (m.base[buffer + i] != uint8_t(0xA0 + i)) return false;  // earlier units untouched
  }
  for (size_t i = 0; i < bytes.size(); ++i) {
    if (m.base[buffer + start + i] != bytes[i]) return false;
  }
  return true;
}
}  // namespace

int main() {
  const std::vector<std::u16string> names = {u"tl_test_texture", u"Second Mod", u"x"};

  // Repaired in one call.
  {
    Memory m;
    m.SetNames(names);
    m.SetStream(16);
    FakeWriter w(m);
    const UnitSaveOutcome out = WriteUnitSave(m.base, kSaveData, kStream, [&] { w(); });
    Check(out.kind == UnitSaveOutcome::Kind::kRepaired && out.names == 3, "repaired");
    Check(w.calls == 1, "repaired: the original ran once");
    Check(StreamHolds(m, kBuffer, 16, Expected(names)), "repaired: lengths in, rest as written");
  }
  // The buffer moves during the call: the repair follows it.
  {
    Memory m;
    m.SetNames(names);
    m.SetStream(16);
    FakeWriter w(m);
    w.move = true;
    const UnitSaveOutcome out = WriteUnitSave(m.base, kSaveData, kStream, [&] { w(); });
    Check(out.kind == UnitSaveOutcome::Kind::kRepaired, "moved buffer: repaired");
    Check(StreamHolds(m, kMovedBuffer, 16, Expected(names)), "moved buffer: repaired where it is now");
  }
  // Fallback: the list is ambiguous; rewound, written again without it, the count restored.
  {
    Memory m;
    m.SetNames(names);
    m.SetStream(16);
    FakeWriter w(m);
    w.twice = true;
    const UnitSaveOutcome out = WriteUnitSave(m.base, kSaveData, kStream, [&] {
      w();
      w.twice = false;  // the decoy only on the first call; the second has no list anyway
    });
    Check(out.kind == UnitSaveOutcome::Kind::kWrittenWithoutList, "fallback taken");
    Check(out.repair == torchlight::mods::ModListRepair::kAmbiguous, "fallback: why");
    Check(w.calls == 2, "fallback: the original ran twice");
    Check(w.counts_seen.size() == 2 && w.counts_seen[0] == 3 && w.counts_seen[1] == 0,
          "fallback: the second call saw no names");
    Check(abi::ReadU32(m.base, kSaveData + 580) == 3, "fallback: the unit's count restored");
    Check(abi::mods::ReadSavedModNames(m.base, kSaveData) == names, "fallback: the names untouched");
    Check(StreamHolds(m, kBuffer, 16, Expected({})), "fallback: position and size as if written once");
  }
  // No names: the original alone, nothing read back or changed.
  {
    Memory m;
    m.SetNames({});
    m.SetStream(16);
    FakeWriter w(m);
    const UnitSaveOutcome out = WriteUnitSave(m.base, kSaveData, kStream, [&] { w(); });
    Check(out.kind == UnitSaveOutcome::Kind::kNoNames && w.calls == 1, "no names: original once");
    Check(StreamHolds(m, kBuffer, 16, Expected({})), "no names: as the original wrote it");
  }

  if (failures) return EXIT_FAILURE;
  std::puts("save_mod_list_test: ok");
  return EXIT_SUCCESS;
}
