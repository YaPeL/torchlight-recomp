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
void WriteUnit(const fs::path& path, const std::string& name, bool commented_name = false,
               const std::string& display_name = "") {
  std::string text = "[UNIT]\r\n";
  if (commented_name) text += "//\t<STRING>NAME:Old " + name + "\r\n";
  text += "\t<STRING>NAME:" + name + "\r\n";
  if (!display_name.empty()) text += "\t<TRANSLATE>DISPLAYNAME:" + display_name + "\r\n";
  text += "\t<STRING>UNIT_GUID:123\r\n[/UNIT]\r\n";
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

  // Names beyond ASCII: UTF-16 layouts read in full, compared in any case, back to UTF-16 intact.
  {
    const std::string name = "Se\xC3\xB1" "ora \xC3\x91" "and\xC3\xBA";  // "Señora Ñandú"
    const std::string upper = "SE\xC3\x91" "ORA \xC3\x91" "AND\xC3\x9A";
    Check(m::ClassKey(name) == upper, "Latin-1 letters compare in any case");
    Check(m::ClassKey("\xE6\x9D\xB1") == "\xE6\x9D\xB1", "other scripts as they are");
    const std::u16string wide = u"Se\u00F1ora \U0001F600";
    Check(m::Utf8ToUtf16(m::Utf16ToUtf8(wide)) == wide, "UTF-16 to UTF-8 and back, a surrogate pair kept");
    Check(m::Utf16ToUtf8(std::u16string{u'a', char16_t(0xD800), u'b'}) == "a\xEF\xBF\xBD" "b", "a lone surrogate is U+FFFD");
    Check(m::Utf8ToUtf16("a\xC3") == u"a\uFFFD", "a cut UTF-8 sequence is U+FFFD");
    // A UTF-16LE layout and a UTF-16BE one.
    const std::string xml = Button("GuiLook/RadioButton", name, "guiSelect1");
    const std::u16string xml16 = m::Utf8ToUtf16(xml);
    std::vector<uint8_t> le = {0xFF, 0xFE}, be = {0xFE, 0xFF};
    for (char16_t c : xml16) {
      le.push_back(uint8_t(c));
      le.push_back(uint8_t(c >> 8));
      be.push_back(uint8_t(c >> 8));
      be.push_back(uint8_t(c));
    }
    Check(m::ClassButtons(m::LayoutText(le)) == std::vector<std::string>({name}), "a UTF-16LE layout's name");
    Check(m::ClassButtons(m::LayoutText(be)) == std::vector<std::string>({name}), "a UTF-16BE layout's name");
    std::vector<uint8_t> utf8 = {0xEF, 0xBB, 0xBF};
    utf8.insert(utf8.end(), xml.begin(), xml.end());
    Check(m::ClassButtons(m::LayoutText(utf8)) == std::vector<std::string>({name}), "a UTF-8 layout's name");
    // Its unit, NAME in upper case: offered.
    Check(m::ClassList({{name}}, {upper}).back() == name, "offered when its unit's NAME matches in any case");
  }

  // Pets: guiPet1 (PC) or GUISELECT2 (Xbox) buttons, the game's three first, monster units only.
  {
    const std::string xml = Button("GuiLook/StandardButton", "Dog", "guiPet1") +
                            Button("GuiLook/StandardButton", "Pet_Wolf", "guiPet1") +
                            Button("GuiLook/StandardButton", "Pet_Ghost", "GUISELECT2") +
                            Button("GuiLook/RadioButton", "Paladin", "guiSelect1");
    Check(m::PetButtons(xml) == std::vector<std::string>({"Dog", "Pet_Wolf", "Pet_Ghost"}), "pet buttons");
    Check(m::PetList({m::PetButtons(xml)}, {"PET_WOLF", "PALADIN"}) ==
              std::vector<std::string>({"Dog", "Cat", "Chakawary", "Pet_Wolf"}),
          "the game's pets first, then the mods' with a monster unit");
    const fs::path root = fs::temp_directory_path() / "class_list_test_pets";
    fs::remove_all(root);
    Write(root / "Pets" / "media" / "UI" / "charactercreate.layout", xml);
    WriteUnit(root / "Pets" / "media" / "units" / "monsters" / "PET_WOLF.DAT", "Pet_Wolf", false, "Dire Wolf");
    WriteUnit(root / "Pets" / "media" / "units" / "monsters" / "PET_GHOST.DAT", "Pet_Ghost");
    WriteUnit(root / "Pets" / "media" / "units" / "players" / "Paladin.dat", "Paladin");
    m::ModPlan plan;
    plan.mods = {{"Pets", 0}};
    Check(m::ModMonsterUnits(root, plan) == std::set<std::string>({"PET_GHOST", "PET_WOLF"}), "monster units only");
    Check(m::PetList(m::ModPetButtons(root, plan), m::ModMonsterUnits(root, plan)) ==
              std::vector<std::string>({"Dog", "Cat", "Chakawary", "Pet_Wolf", "Pet_Ghost"}),
          "a mod's pets offered");
    const auto shown = m::ModMonsterDisplayNames(root, plan);
    Check(shown.size() == 2 && shown.at("PET_WOLF") == "Dire Wolf", "a pet's label: its DISPLAYNAME");
    Check(shown.size() == 2 && shown.at("PET_GHOST") == "Pet_Ghost", "its NAME without one");
    fs::remove_all(root);
  }

  // A mod class's own icon (option B): the layout button's images, the imageset with its image
  // file in the mod, every image present; otherwise no icon (the frame with the name).
  {
    const std::string radio =
        "<Window Type=\"GuiLook/RadioButton\" Name=\"Paladin\">\n"
        "  <Property Name=\"SelectedImage\" Value=\"set:PalLogo image:palsel\" />\n"
        "  <Property Name=\"HoverImage\" Value=\"set:PalLogo image:pallit\" />\n"
        "  <Property Name=\"UnselectedImage\" Value=\"set:PalLogo image:pal\" />\n"
        "  <Property Name=\"onClick\" Value=\"guiSelect1\" />\n"
        "</Window>\n"
        "<Window Type=\"GuiLook/RadioButton\" Name=\"Monk\">\n"
        "  <Property Name=\"UnselectedImage\" Value=\"set:PalLogo image:monk\" />\n"
        "  <Property Name=\"HoverImage\" Value=\"set:Other image:monklit\" />\n"
        "  <Property Name=\"onClick\" Value=\"guiSelect1\" />\n"
        "</Window>\n"
        "<Window Type=\"GuiLook/RadioButton\" Name=\"Druid\">\n"
        "  <Property Name=\"UnselectedImage\" Value=\"set:PalLogo image:druid\" />\n"
        "  <Property Name=\"onClick\" Value=\"guiSelect1\" />\n"
        "</Window>\n"
        "<Window Type=\"GuiLook/RadioButton\" Name=\"Ghost\">\n"
        "  <Property Name=\"UnselectedImage\" Value=\"set:PalLogo image:ghost\" />\n"
        "  <Property Name=\"onClick\" Value=\"guiSelect1\" />\n"
        "</Window>\n"
        "<Window Type=\"GuiLook/StandardButton\" Name=\"Pet_X\">\n"
        "  <Property Name=\"NormalImage\" Value=\"set:PalLogo image:pal\" />\n"
        "  <Property Name=\"onClick\" Value=\"guiPet1\" />\n"
        "</Window>\n";
    const auto images = m::ClassButtonImages(radio);
    Check(images.size() == 3 && images.at("PALADIN").imageset == "PalLogo" && images.at("PALADIN").normal == "pal" &&
              images.at("PALADIN").hover == "pallit" && images.at("PALADIN").pushed == "palsel",
          "a radio button's three images");
    Check(!images.contains("MONK"), "images from two imagesets: none");
    Check(images.at("DRUID").hover == "druid" && images.at("DRUID").pushed == "druid", "a lone image for all three");
    Check(!images.contains("PET_X"), "only class buttons");
    const auto pet_images = m::PetButtonImages(radio);
    Check(pet_images.size() == 1 && pet_images.at("PET_X").imageset == "PalLogo" &&
              pet_images.at("PET_X").normal == "pal" && pet_images.at("PET_X").hover == "pal",
          "a pet button's images, by the pet buttons' commands only");

    const std::string set_xml =
        "<?xml version=\"1.0\"?>\n<Imageset Name=\"PalLogo\" Imagefile=\"media\\UI\\Icons\\pal.png\" >\n"
        "  <Image Name=\"pal\" XPos=\"0\" YPos=\"0\" Width=\"8\" Height=\"8\" />\n"
        "  <Image Name=\"pallit\" XPos=\"0\" YPos=\"8\" Width=\"8\" Height=\"8\" />\n"
        "  <Image Name=\"palsel\" XPos=\"0\" YPos=\"16\" Width=\"8\" Height=\"8\" />\n"
        "  <Image Name=\"druid\" XPos=\"8\" YPos=\"0\" Width=\"8\" Height=\"8\" />\n"
        "</Imageset>\n";
    const auto info = m::ParseImageset(set_xml);
    Check(info && info->name == "PalLogo" && info->image_file == "media\\UI\\Icons\\pal.png" &&
              info->images.size() == 4,
          "an imageset's name, file and images");
    Check(!m::ParseImageset("<Imageset Imagefile=\"a.png\"></Imageset>"), "no Name: none");
    const auto spaced = m::ParseImageset("<Imageset Name= \"widgets\" Imagefile = 'w.dds'>\n\t<Image Name =\"a\" />");
    Check(spaced && spaced->name == "widgets" && spaced->image_file == "w.dds" && spaced->images.contains("a"),
          "spaces around '=' and single quotes, as XML allows");

    const fs::path root = fs::temp_directory_path() / "class_list_test_icons";
    fs::remove_all(root);
    Write(root / "Classes" / "media" / "UI" / "charactercreate.layout", radio);
    Write(root / "Classes" / "media" / "UI" / "itemicons" / "pallogo.imageset", set_xml);
    const std::string png("\x89PNG\r\n\x1a\n....", 12);
    Write(root / "Classes" / "media" / "UI" / "ICONS" / "Pal.PNG", png);
    // A second mod with an imageset of the same name: the first in the search order is kept.
    Write(root / "Later" / "media" / "UI" / "x.imageset",
          "<Imageset Name=\"PalLogo\" Imagefile=\"media/ui/x.png\"><Image Name=\"ghost\" /></Imageset>");
    Write(root / "Later" / "media" / "UI" / "x.png", png);
    m::ModPlan plan;
    plan.mods = {{"Classes", 0}, {"Later", 1}};
    const auto sets = m::ModImagesets(root, plan);
    Check(sets.empty(), "a name two mods define: left out (which one the game loaded is not known)");
    fs::remove_all(root / "Later");
    plan.mods = {{"Classes", 0}};
    const auto one = m::ModImagesets(root, plan);
    Check(one.size() == 1 && one.at("PalLogo").mod_folder == "Classes" &&
              one.at("PalLogo").file == "media/UI/itemicons/pallogo.imageset",
          "imagesets by name, the image file found in any case");
    Write(root / "Classes" / "media" / "UI" / "y.imageset", "<Imageset Name=\"Gone\" Imagefile=\"media/ui/gone.png\"></Imageset>");
    Check(m::ModImagesets(root, plan).size() == 1, "one without its image file left out");
    Write(root / "Classes" / "media" / "UI" / "z.imageset", "<Imageset Name=\"Broken\" Imagefile=\"media/ui/z.png\"></Imageset>");
    Write(root / "Classes" / "media" / "UI" / "z.png", "not an image");
    Check(m::ModImagesets(root, plan).size() == 1, "one whose image file is not an image format left out");
    Write(root / "Classes" / "media" / "UI" / "z.png", "DDS |...");
    Check(m::ModImagesets(root, plan).size() == 2, "a DDS by its signature");
    fs::remove(root / "Classes" / "media" / "UI" / "z.imageset");
    const auto& sets_used = one;
    const auto icons = m::ModClassIcons(root, plan, sets_used);
    Check(icons.size() == 2 && icons.contains("PALADIN") && icons.contains("DRUID"),
          "classes with every image in the imageset have an icon");
    Check(!icons.contains("GHOST"), "an image the imageset does not hold: no icon (the frame with the name)");
    const auto pet_icons = m::ModPetIcons(root, plan, sets_used);
    Check(pet_icons.size() == 1 && pet_icons.contains("PET_X") && pet_icons.at("PET_X").imageset.mod_folder == "Classes",
          "a mod pet with its images has an icon");
    fs::remove_all(root);
  }

  if (failures) {
    std::fprintf(stderr, "%d failure(s)\n", failures);
    return EXIT_FAILURE;
  }
  std::printf("class_list_test: ok\n");
  return EXIT_SUCCESS;
}
