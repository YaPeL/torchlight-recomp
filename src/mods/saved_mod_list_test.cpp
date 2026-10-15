// The repair of a character save's mod list (saved_mod_list.h) on synthetic stream bytes: the list
// as the Xbox writer leaves it (lengths 0) among other fields, found only with its anchor and exact
// count, repaired to what the reader and PC expect, and left alone in every other case.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "mods/saved_mod_list.h"

using namespace torchlight::mods;

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
  if (!ok) {
    ++failures;
    std::fprintf(stderr, "FAIL: %s\n", what);
  }
}

using Bytes = std::vector<uint8_t>;

Bytes Concat(std::initializer_list<Bytes> parts) {
  Bytes out;
  for (const Bytes& p : parts) out.insert(out.end(), p.begin(), p.end());
  return out;
}

// Stand-ins for the fields the unit writer puts around the list.
const Bytes kHead = {0x00, 0x00, 0x00, 0x0A, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
const Bytes kTail = {0x00, 0x00, 0x00, 0x05, 0x14, 0x47, 0x34, 0x58, 0x00, 0x14, 0x00, 0x48};
constexpr uint32_t kBefore = 0xFFFFFFFF;
}  // namespace

int main() {
  const std::vector<std::u16string> names = {u"first_mod", u"Second Mod", u"x"};

  // The bug's bytes: lengths 0, the units big-endian.
  const Bytes buggy = EncodeSavedModList(kBefore, names, true);
  const Bytes good = EncodeSavedModList(kBefore, names, false);
  Check(buggy.size() == good.size(), "same size with and without the lengths");
  Check(buggy[8] == 0 && buggy[9] == 0 && buggy[10] == 0x00 && buggy[11] == 'f', "bug: length 0, then 'f'");
  Check(good[8] == 0 && good[9] == 9 && good[10] == 0x00 && good[11] == 'f', "reader: u16 9, then 'f'");

  // Repaired in place, nothing else touched.
  {
    Bytes stream = Concat({kHead, buggy, kTail});
    Check(RepairSavedModList(stream, kBefore, names) == ModListRepair::kRepaired, "repaired");
    Check(stream == Concat({kHead, good, kTail}), "lengths written, other bytes unchanged");
    Check(RepairSavedModList(stream, kBefore, names) == ModListRepair::kNotFound,
          "an already correct list is not found again");
  }
  // A one-name list whose name ends where the next field starts with zeros.
  {
    const std::vector<std::u16string> one = {u"tl_test_texture"};
    Bytes stream = Concat({kHead, EncodeSavedModList(kBefore, one, true), kTail});
    Check(RepairSavedModList(stream, kBefore, one) == ModListRepair::kRepaired, "one name repaired");
    Check(stream == Concat({kHead, EncodeSavedModList(kBefore, one, false), kTail}), "one name bytes");
  }
  // Look-alikes earlier in the written bytes: the same names under another anchor, and a name whose
  // units read like a zero length plus the next name. Only the real list matches.
  {
    const std::vector<std::u16string> tricky = {std::u16string(u"a\0b", 3), u"b"};
    const Bytes decoy_anchor = EncodeSavedModList(0x12345678, tricky, true);
    const Bytes decoy_count = EncodeSavedModList(kBefore, {std::u16string(u"a\0b", 3)}, true);
    Bytes stream = Concat({decoy_anchor, decoy_count, kHead, EncodeSavedModList(kBefore, tricky, true), kTail});
    const Bytes expected =
        Concat({decoy_anchor, decoy_count, kHead, EncodeSavedModList(kBefore, tricky, false), kTail});
    Check(RepairSavedModList(stream, kBefore, tricky) == ModListRepair::kRepaired, "decoys ignored");
    Check(stream == expected, "only the real list changed");
  }
  // Left alone: another count, twice, not there, too long, nothing to do.
  {
    const Bytes original = Concat({kHead, EncodeSavedModList(kBefore, {u"first_mod", u"Second Mod"}, true), kTail});
    Bytes stream = original;
    Check(RepairSavedModList(stream, kBefore, names) == ModListRepair::kNotFound, "other count: not found");
    Check(stream == original, "other count: unchanged");
  }
  {
    const Bytes original = Concat({kHead, buggy, kTail, buggy});
    Bytes stream = original;
    Check(RepairSavedModList(stream, kBefore, names) == ModListRepair::kAmbiguous, "twice: ambiguous");
    Check(stream == original, "twice: unchanged");
  }
  {
    Bytes stream = Concat({kHead, kTail});
    const Bytes original = stream;
    Check(RepairSavedModList(stream, kBefore, names) == ModListRepair::kNotFound, "absent: not found");
    Check(stream == original, "absent: unchanged");
  }
  {
    Bytes stream = Concat({kHead, buggy, kTail});
    const Bytes original = stream;
    Check(RepairSavedModList(stream, kBefore, {std::u16string(0x10000, u'm')}) == ModListRepair::kTooLong,
          "too long");
    Check(stream == original, "too long: unchanged");
    Check(RepairSavedModList(stream, kBefore, {}) == ModListRepair::kNothing, "no names: nothing");
    Check(stream == original, "no names: unchanged");
  }

  // Repair at load: the names found by the candidates, the split the save accepts.
  {
    const std::vector<std::u16string> names = {u"Alpha", u"Beta Mod"};
    std::vector<uint8_t> good = {0xAA, 0xBB};
    const auto list = EncodeSavedModList(7, names, false);
    good.insert(good.end(), list.begin(), list.end());
    good.push_back(0xCC);
    std::vector<uint8_t> broken = {0xAA, 0xBB};
    const auto bug = EncodeSavedModList(7, names, true);
    broken.insert(broken.end(), bug.begin(), bug.end());
    broken.push_back(0xCC);
    auto reads_like_good = [&](std::span<const uint8_t> body) {
      return std::vector<uint8_t>(body.begin(), body.end()) == good;
    };
    std::string why;
    const auto fixed = RepairModListAtLoad(broken, {u"Beta", u"Beta Mod", u"Alpha"}, reads_like_good, &why);
    Check(fixed && *fixed == good, "repaired with the names, a prefix name tried and refused");
    Check(!RepairModListAtLoad(broken, {u"Alpha"}, reads_like_good, &why) &&
              why == "no list of known mod names with empty lengths in it",
          "a name missing from the candidates: not repaired");
    // Two splits that both read: left alone.
    std::vector<uint8_t> one = {0, 0, 0, 1, 0, 0, 0, 'A', 0, 'B'};
    Check(!RepairModListAtLoad(one, {u"A", u"AB"}, [](std::span<const uint8_t>) { return true; }, &why) &&
              why == "more than one repair of the list makes the save read",
          "ambiguous: left alone");
    Check(!RepairModListAtLoad(broken, names, [](std::span<const uint8_t>) { return false; }, &why) &&
              why == "no repair of the list makes the save read",
          "nothing that reads: left alone");
  }

  if (failures) return EXIT_FAILURE;
  std::puts("saved_mod_list_test: ok");
  return EXIT_SUCCESS;
}
