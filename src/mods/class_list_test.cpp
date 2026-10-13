// Tests for the creation screen's class list (class_list.h) on synthetic layouts and mod folders.

#include "mods/class_list.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

namespace m = torchlight::mods;
namespace fs = std::filesystem;

namespace {

int failures = 0;
void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

std::string Button(const std::string& type, const std::string& name, const std::string& click) {
  return "<Window Type=\"" + type + "\" Name=\"" + name + "\">\n"
         "  <Property Name=\"UnifiedSize\" Value=\"{{0,64},{0,64}}\" />\n"
         "  <Property Name=\"onClick\" Value=\"" + click + "\"/>\n"
         "</Window>\n";
}

void Write(const fs::path& path, const std::string& text) {
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << text;
}

// A UTF-16LE text definition, as mods ship them.
void WriteUnit(const fs::path& path, const std::string& name, bool commented_name = false) {
  std::string text = "[UNIT]\r\n";
  if (commented_name) text += "//\t<STRING>NAME:Old " + name + "\r\n";
  text += "\t<STRING>NAME:" + name + "\r\n\t<STRING>UNIT_GUID:123\r\n[/UNIT]\r\n";
  std::string bytes = "\xFF\xFE";
  for (char c : text) {
    bytes.push_back(c);
    bytes.push_back('\0');
  }
  Write(path, bytes);
}

}  // namespace

int main() {
  // Buttons: only guiSelect1, any case, in order; pets (guiSelect2) and the rest left out.
  {
    const std::string xml = "<GUILayout><Window Type=\"DefaultWindow\" Name=\"Root\">\n" +
                            Button("GuiLook/RadioButton", "Destroyer", "guiSelect1") +
                            Button("GuiLook/RadioButton", "Lady Knight", "GUISELECT1") +
                            Button("GuiLook/StandardButton", "Dog", "guiSelect2") +
                            Button("GuiLook/StandardButton", "CreatePlayer", "guiNewGame") +
                            "<Window Type=\"GuiLook/RadioButton\" Name=\"Nested\">"
                            "<Window Type=\"x\" Name=\"Child\"><Property Name=\"onClick\" Value=\"guiSelect1\"/></Window>"
                            "</Window>"
                            "</Window></GUILayout>";
    const auto buttons = m::ClassButtons(xml);
    Check(buttons == std::vector<std::string>({"Destroyer", "Lady Knight", "Child"}),
          "class buttons in order; a child's onClick is the child's");
  }

  // The list: the game's classes first, then the mods' in order, no repeats, only player units.
  {
    const std::set<std::string> players = {"DESTROYER", "ENCHANTRESS", "LADY KNIGHT", "STONEBROTHER"};
    const auto list = m::ClassList({{"Destroyer", "Vanquisher", "Alchemist", "Lady Knight", "Demonologist"},
                                    {"Stonebrother", "Lady Knight", "Enchantress"}},
                                   players);
    Check(list == std::vector<std::string>({"Destroyer", "Alchemist", "Vanquisher", "Lady Knight",
                                            "Stonebrother", "Enchantress"}),
          "game classes first, then the mods' in order, without repeats or missing units");
    Check(m::ClassList({}, {}) == std::vector<std::string>({"Destroyer", "Alchemist", "Vanquisher"}),
          "without mods: the game's three");
  }

  // Mod folders: enabled mods in search order, layouts and player units found in any case.
  {
    const fs::path root = fs::temp_directory_path() / "class_list_test";
    fs::remove_all(root);
    Write(root / "Classes" / "MEDIA" / "UI" / "CharacterCreate.layout",
          Button("GuiLook/RadioButton", "Destroyer", "guiSelect1") +
              Button("GuiLook/RadioButton", "Enchantress", "guiSelect1") +
              Button("GuiLook/RadioButton", "Ghost", "guiSelect1"));
    WriteUnit(root / "Classes" / "MEDIA" / "units" / "players" / "Enchantress" / "Enchantress.dat",
              "Enchantress", true);
    Write(root / "Off" / "media" / "UI" / "charactercreate.layout",
          Button("GuiLook/RadioButton", "Hidden", "guiSelect1"));
    WriteUnit(root / "Off" / "media" / "units" / "players" / "Hidden.dat", "Hidden");
    WriteUnit(root / "Units" / "media" / "Units" / "Players" / "Ghost.DAT", "Ghost");
    m::ModPlan plan;
    plan.mods = {{"Units", 0}, {"Classes", 1}, {"Off", -1}};
    const auto units = m::ModPlayerUnits(root, plan);
    Check(units == std::set<std::string>({"ENCHANTRESS", "GHOST"}),
          "player units of the enabled mods, by NAME (a commented line skipped)");
    const auto buttons = m::ModClassButtons(root, plan);
    Check(buttons.size() == 1 && buttons[0].size() == 3, "the enabled mods' layouts");
    Check(m::ClassList(buttons, units) ==
              std::vector<std::string>({"Destroyer", "Alchemist", "Vanquisher", "Enchantress", "Ghost"}),
          "a disabled mod's class is not offered");
    fs::remove_all(root);
  }

  if (failures) {
    std::fprintf(stderr, "%d failure(s)\n", failures);
    return EXIT_FAILURE;
  }
  std::printf("class_list_test: ok\n");
  return EXIT_SUCCESS;
}
