// Tests for the schema port and the tree reader/writer, against fixtures the Python tool made
// (tests/save_import/fixtures, from tests/save_convert/make_fixtures.py; synthetic, no game data).

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>

#include "save_import/json.h"
#include "save_import/save_tree.h"
#include "save_import/schema.h"

namespace {

using namespace torchlight::save_import;

void Check(bool ok, const std::string& what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    std::exit(1);
  }
}

Bytes Fixture(const char* name) {
  std::ifstream file(std::filesystem::path(SAVE_IMPORT_FIXTURES) / name, std::ios::binary);
  Check(bool(file), std::string("fixture ") + name);
  return Bytes(std::istreambuf_iterator<char>(file), {});
}

void TestJson() {
  std::string error;
  auto json = Json::Parse(R"({"a": [1, -2, {"b": "x\"é"}], "t": true, "n": null})", error);
  Check(json && json->is_object(), "json parses: " + error);
  Check(json->Find("a")->items()[1]->integer() == -2, "json integer");
  Check(json->Find("a")->items()[2]->Find("b")->string() == "x\"\xc3\xa9", "json string escapes");
  Check(json->Find("t")->boolean(), "json bool");
  Check(!Json::Parse("{\"a\": 1.5}", error) && error.find("integers") != std::string::npos,
        "json refuses floats");
  Check(!Json::Parse("[1, 2", error), "json refuses truncated text");
}

void TestEmbeddedSchema(const Schema& schema) {
  Check(schema.root() && schema.root()->name == "character", "root struct");
  Check(schema.stash_root() && schema.stash_root()->name == "shared_stash", "stash root struct");
  Check(schema.Find("item") != nullptr, "item struct");
}

void TestCharacter(const Schema& schema) {
  const Bytes pc = Fixture("pc_save.svt");
  SaveError error;
  auto parsed = ReadPc(schema, pc, error);
  Check(parsed != nullptr, "reads the PC save: " + error.message);
  Check(parsed->version == kPcVersion, "PC version");
  Check(!parsed->refs.empty(), "references found");

  // Written back little-endian it is the same body; big-endian, the Python tool's 360 file.
  std::span<const uint8_t> body;
  Check(SplitPc(pc, body, error), "PC trailer");
  const Bytes little = WriteBody(schema, *schema.root(), parsed->tree, Endian::kLittle);
  Check(little == Bytes(body.begin(), body.end()), "PC body round trip");
  Check(Write360(schema, *parsed) == Fixture("pc_save.expected.tsv"), "same 360 file as Python");

  // A native v25 save reads and writes back unchanged.
  const Bytes native = Fixture("native_v25.tsv");
  std::span<const uint8_t> native_body;
  Check(Split360(native, native_body, error), "360 digest: " + error.message);
  auto x360 = ParseBody(schema, *schema.root(), native_body, Endian::kBig, error);
  Check(x360 && x360->version == 25u, "reads the v25 save: " + error.message);
  Check(WriteBody(schema, *schema.root(), x360->tree, Endian::kBig) ==
            Bytes(native_body.begin(), native_body.end()),
        "v25 round trip");
}

// A PC character saved with mods records their names (player/names): the import writes each one
// as the 360 reader takes it, u16 length then UTF-16 units, big-endian (docs/mods.md, 7d).
void TestModList(const Schema& schema) {
  SaveError error;
  auto parsed = ReadPc(schema, Fixture("pc_save.svt"), error);
  Check(parsed != nullptr, "reads the PC save: " + error.message);
  Node* names = parsed->tree.Find("player") ? parsed->tree.Find("player")->Find("names") : nullptr;
  Check(names && names->kind == Node::Kind::kList, "player/names is a list");
  const std::u16string mods[] = {u"First Mod", u"another_mod", u"Z"};
  names->items.clear();
  for (const std::u16string& m : mods) {
    Node text;
    text.kind = Node::Kind::kText;
    text.text = m;
    names->items.push_back(text);
  }
  const Bytes x360 = Write360(schema, *parsed);
  std::span<const uint8_t> body;
  Check(Split360(x360, body, error), "360 digest: " + error.message);

  Bytes expected = {0, 0, 0, 3};
  for (const std::u16string& m : mods) {
    expected.push_back(static_cast<uint8_t>(m.size() >> 8));
    expected.push_back(static_cast<uint8_t>(m.size()));
    for (char16_t c : m) {
      expected.push_back(static_cast<uint8_t>(c >> 8));
      expected.push_back(static_cast<uint8_t>(c));
    }
  }
  int found = 0;
  for (auto it = body.begin();; ++it) {
    it = std::search(it, body.end(), expected.begin(), expected.end());
    if (it == body.end()) break;
    ++found;
  }
  Check(found == 1, "the names with their u16 lengths, big-endian, once");

  auto back = ParseBody(schema, *schema.root(), body, Endian::kBig, error);
  Check(back != nullptr, "the 360 side reads back: " + error.message);
  const Node* read = back->tree.Find("player")->Find("names");
  Check(read->items.size() == 3, "three names read back");
  auto it = read->items.begin();
  for (const std::u16string& m : mods) Check((it++)->text == m, "name read back");
}

void TestStash(const Schema& schema) {
  const Bytes pc = Fixture("pc_stash.bin");
  SaveError error;
  auto parsed = ReadPcStash(schema, pc, error);
  Check(parsed != nullptr, "reads the PC stash: " + error.message);
  Check(Write360Stash(schema, *parsed) == Fixture("pc_stash.expected.bin"), "same stash as Python");
  auto back = Read360Stash(schema, Fixture("pc_stash.expected.bin"), error);
  Check(back != nullptr, "reads the 360 stash: " + error.message);
}

void TestErrors(const Schema& schema) {
  SaveError error;
  Bytes pc = Fixture("pc_save.svt");
  pc.pop_back();
  Check(!ReadPc(schema, pc, error) && error.message.find("does not match the file size") !=
                                          std::string::npos,
        "length trailer checked");

  Bytes native = Fixture("native_v25.tsv");
  native[10] ^= 1;
  std::span<const uint8_t> body;
  Check(!Split360(native, body, error) && error.message.find("SHA-256") != std::string::npos,
        "digest checked");

  // A PC save of another version is refused before parsing.
  Bytes other = Fixture("pc_save.svt");
  other[0] = 25;
  Check(!ReadPc(schema, other, error) && error.message.find("unsupported save version 25") !=
                                             std::string::npos,
        "version checked");

  // One byte less in the body: the reader runs out of data or misreads; it must not crash.
  Bytes cut = Fixture("pc_save.svt");
  cut.erase(cut.end() - 5);
  const uint32_t size = static_cast<uint32_t>(cut.size());
  for (int i = 0; i < 4; ++i) cut[cut.size() - 4 + i] = static_cast<uint8_t>(size >> (8 * i));
  Check(!ReadPc(schema, cut, error) && !error.message.empty(), "truncated body refused");
}

}  // namespace

int main() {
  TestJson();
  std::string error;
  auto schema = Schema::Embedded(error);
  Check(schema != nullptr, "embedded schema: " + error);
  TestEmbeddedSchema(*schema);
  TestCharacter(*schema);
  TestModList(*schema);
  TestStash(*schema);
  TestErrors(*schema);
  std::puts("save_import save_tree: ok");
  return 0;
}
