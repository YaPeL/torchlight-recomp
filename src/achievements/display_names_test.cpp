// Every catalog ID has its own English text, and data/ui/tl_achievement_strings.txt translates all
// of them, plus the title, to every shipped language within what the game's UI can draw.
#include "achievements/display_names.h"
#include "game_menu/menu_strings.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
namespace pc=torchlight::achievements;
namespace {
int failures=0;
void Check(bool value,const std::string& message) { if(!value) { ++failures; std::cerr<<message<<'\n'; } }
// Latin-1 only: the game's UTF-16 to CEGUI conversion keeps the low 8 bits (game_ui.h).
bool Latin1(std::string_view utf8) {
  for (size_t i=0;i<utf8.size();++i) {
    const auto c=uint8_t(utf8[i]);
    if (c<0x80) continue;
    if ((c==0xC2 || c==0xC3) && i+1<utf8.size() && (uint8_t(utf8[i+1])&0xC0)==0x80) { ++i; continue; }
    return false;
  }
  return true;
}
}
int main() {
  std::set<std::string_view> texts;
  for (size_t i=0;i<pc::kCatalog.size();++i) {
    const auto& name=pc::kDisplayNames[i];
    Check(name.id==pc::kCatalog[i].id,"display name order differs from the catalog at "+std::string(name.id));
    Check(!name.english.empty() && name.english.find('=')==std::string_view::npos,"unusable text for "+std::string(name.id));
    Check(texts.insert(name.english).second,"duplicate text "+std::string(name.english));
    Check(pc::EnglishName(name.id)==name.english,"lookup of "+std::string(name.id));
  }
  Check(pc::EnglishName("NOT_AN_ACHIEVEMENT")=="NOT_AN_ACHIEVEMENT","unknown ID falls back to itself");
  std::ifstream file(TL_ACHIEVEMENT_STRINGS_FILE);
  std::stringstream text; text<<file.rdbuf();
  std::vector<std::string> warnings;
  torchlight::game_menu::MenuStrings strings;
  strings.Parse(text.str(),&warnings);
  Check(file.good() && warnings.empty(),"strings file loads without warnings");
  texts.insert(pc::kUnlockedTitle);
  // The achievement list's own texts (achievement_list.cpp).
  for (const char* t:{"Achievements (PC, incomplete)","Unlocked",
                      "Not available in this version","Close"}) texts.insert(t);
  for (const char* language:{"de","fr","es"}) {
    for (const auto english:texts) {
      const std::string translated=strings.Translate(language,std::string(english));
      Check(translated!=english,std::string(language)+" lacks "+std::string(english));
      Check(Latin1(translated),std::string(language)+" text beyond Latin-1: "+translated);
    }
  }
  return failures?EXIT_FAILURE:EXIT_SUCCESS;
}
