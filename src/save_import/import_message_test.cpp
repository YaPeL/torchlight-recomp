// Tests for the import message boxes: sections, buttons, counts for long lists, notices, and that
// every English text the code uses has a translation in each language of tl_import_strings.txt.

#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

#include "game_menu/menu_strings.h"
#include "save_import/import_message.h"

namespace {

using namespace torchlight::save_import;

void Check(bool ok, const std::string& what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    std::exit(1);
  }
}

bool Has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

std::set<std::string> g_used;  // every English text asked for
std::string Record(const std::string& english) {
  g_used.insert(english);
  return english;
}

CharacterImport Character(const char* file, const char* name, int number, bool ok = true) {
  CharacterImport c;
  c.source = file;
  c.name = name;
  c.class_name = "Vanquisher";
  c.number = number;
  if (!ok) c.problems = {"unit (UNIT_GUID) 5 at player/items/item/unit_guid"};
  return c;
}

void TestFullPlan() {
  ImportPlan plan;
  plan.characters = {Character("0.SVT", "butita", 3), Character("1.SVT", "Player", 4),
                     Character("2.SVT", "Modded", -1, false)};
  StashImport stash;
  stash.source = "SHAREDSTASH.BIN";
  stash.destination = "sharedstash.bin";
  stash.pc_items = 8;
  plan.stashes = {stash};
  plan.settings.emplace();
  const ImportMessage m = MainImportMessage(plan, "/games/torchlight/import", Record);
  Check(m.buttons.size() == 3 && m.active == 1, "Yes / Not now / Do not ask again, Not now active");
  Check(Has(m.text, "Imported now:\n  butita (Vanquisher), slot 3\n  Player (Vanquisher), slot 4\n"),
        "characters imported now: " + m.text);
  Check(Has(m.text, "Applied the next time you open the game:\n  Shared stash (8 items)\n  Settings"),
        "stash and settings at the next start: " + m.text);
  Check(Has(m.text, "Cannot be imported (see import.log in the import folder):\n  2.SVT\n"), "rejected");
  Check(MainImportAnswer(plan, 0) == MainAnswer::kYes && MainImportAnswer(plan, 1) == MainAnswer::kNotNow &&
            MainImportAnswer(plan, 2) == MainAnswer::kDoNotAskAgain,
        "answers");
}

void TestLongList() {
  ImportPlan plan;
  for (int i = 0; i < 9; ++i) plan.characters.push_back(Character("x.SVT", "Hero", i));
  const ImportMessage m = MainImportMessage(plan, "/import", Record);
  Check(Has(m.text, "Imported now:\n  9 characters\n") && !Has(m.text, "slot 8"), "counted: " + m.text);
}

void TestBlocked() {
  ImportPlan plan;
  plan.missing_pc_pak = true;
  plan.blocked = "the PC Pak.zip is not in the import folder";
  ImportMessage m = MainImportMessage(plan, "/games/torchlight/import", Record);
  Check(Has(m.text, "Pak.zip") && Has(m.text, "/games/torchlight/import"), "says where to put Pak.zip");
  Check(m.buttons.size() == 2 && m.active == 0, "Not now / Do not ask again");
  Check(MainImportAnswer(plan, 0) == MainAnswer::kNotNow &&
            MainImportAnswer(plan, 1) == MainAnswer::kDoNotAskAgain,
        "blocked answers");
  plan.missing_pc_pak = false;
  plan.blocked = "could not open Pak.zip as a game pak: not a zip";
  m = MainImportMessage(plan, "/import", Record);
  Check(Has(m.text, "not a zip"), "says why");
}

void TestNotices() {
  ImportPlan plan;
  plan.notices = {{Notice::Kind::kStashChanged, "sharedstash.bin", 12, 0},
                  {Notice::Kind::kContainerMissing, "", 0, 0},
                  {Notice::Kind::kUnreadable, "settings.txt", 0, 0}};
  StashImport stash;
  stash.source = "sharedstash.bin";
  stash.destination = "sharedstash.bin";
  stash.pc_items = 8;
  stash.recomp_items = 12;
  plan.stashes = {stash};
  const ImportMessage m = MainImportMessage(plan, "/import", Record);
  Check(Has(m.text, "now has 12 items (0 when you confirmed)"), "explains the stash: " + m.text);
  Check(Has(m.text, "no longer exists") && Has(m.text, "settings.txt could not be read"), "other notices");

  const ImportMessage replace = ReplaceStashMessage(stash, Record);
  Check(replace.buttons.size() == 2 && replace.active == 0, "No active");
  Check(Has(replace.text, "The shared stash already has 12 items") && Has(replace.text, "(8 items)"),
        "replace text: " + replace.text);
  stash.destination = "sharedstashh.bin";
  Check(Has(ReplaceStashMessage(stash, Record).text, "hardcore shared stash"), "hardcore");
  stash.pc_items = 2;
  plan.stashes = {stash};
  Check(Has(MainImportMessage(plan, "/import", Record).text, "Hardcore shared stash (2 items)"), "hardcore line");
}

void TestTranslations() {
  torchlight::game_menu::MenuStrings strings;
  std::vector<std::string> warnings;
  std::string error;
  Check(strings.Load(TL_IMPORT_STRINGS_FILE, warnings, error), "loads: " + error);
  Check(warnings.empty(), "no malformed lines");
  for (const char* language : {"de", "fr", "es"}) {
    for (const auto& english : g_used) {
      const std::string translated = strings.Translate(language, english);
      Check(translated != english || english == "No",
            std::string("missing ") + language + " translation: " + english);
      // Every placeholder survives the translation.
      for (size_t at = english.find('{'); at != std::string::npos; at = english.find('{', at + 1)) {
        const std::string placeholder = english.substr(at, english.find('}', at) - at + 1);
        Check(Has(translated, placeholder), std::string(language) + " drops " + placeholder + ": " + translated);
      }
    }
  }
}

}  // namespace

int main() {
  TestFullPlan();
  TestLongList();
  TestBlocked();
  TestNotices();
  TestTranslations();
  std::puts("save_import import_message: ok");
  return 0;
}
