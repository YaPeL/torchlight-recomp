// English text shown when an achievement unlocks: our own descriptions of each condition
// (docs/achievements.md), written for this project. They are not the official PC or Xbox names,
// which stay in the user's game files and Steam and must not be copied here. Translations live in
// data/ui/tl_achievement_strings.txt.
#pragma once
#include <array>
#include <string_view>
#include "achievements/catalog.h"
namespace torchlight::achievements {
inline constexpr std::string_view kUnlockedTitle = "Achievement Unlocked";
struct DisplayName { std::string_view id; std::string_view english; };
// Same order as kCatalog.
inline constexpr std::array<DisplayName, kCatalog.size()> kDisplayNames{{
    {"TORCHLIGHT_ACHIEVEMENT_FIRSTLEVEL", "Entered the first dungeon"},
    {"BREAKABLES", "Smashed 1,500 breakable objects"},
    {"BEAST_OF_BURDEN", "Filled your pet's inventory"},
    {"PLAYER_GOLD_COLLECTED", "Collected 250,000 gold"},
    {"PLAYER_GOLD_IN_POCKET", "Carried 100,000 gold at once"},
    {"CRITICAL_STRIKE_ON_DEATH", "Made 25 enemies explode"},
    {"MAX_DMG_DONE", "Dealt 10,000 damage in a single hit"},
    {"REACH_LVL_50", "Reached dungeon floor 50"},
    {"REACH_LVL_100", "Reached dungeon floor 100"},
    {"PET_TRAINER", "Taught your pet a spell"},
    {"PET_MIMIC", "Turned your pet into a mimic"},
    {"ENCHANTER_SUCCESS_5", "Enchanted one item 5 times"},
    {"ENCHANTER_SUCCESS_10", "Enchanted one item 10 times"},
    {"ENCHANTER_FAILURE_FIRST", "An enchantment failed on an unenchanted item"},
    {"ENCHANTER_FAILURE_20", "Suffered 20 failed enchantments"},
    {"GAMBLER_20", "Gambled 20 times"},
    {"GAMBLER_50", "Gambled 50 times"},
    {"GAMBLER_100", "Gambled 100 times"},
    {"MODS_1", "Played with a mod"},
    {"MODS_5", "Played with 5 mods"},
    {"MODS_10", "Played with 10 mods"},
    {"PLAYER_LEVEL_65", "Reached character level 65"},
    {"PLAYER_LEVEL_100", "Reached character level 100"},
    {"PULLED_LEVERS_100", "Pulled 100 levers"},
    {"PET_POTIONS_50", "Gave your pet 50 potions"},
    {"PET_SEND_TO_TOWN", "Sent your pet to town"},
    {"FISH_CAUGHT_50", "Caught 50 fish"},
    {"FISH_CAUGHT_100", "Caught 100 fish"},
    {"FISH_CAUGHT_1000", "Caught 1,000 fish"},
    {"PET_FEED_FISH_ANY", "Fed a fish to your pet"},
    {"PET_FEED_FISH_PERMANENT", "Transformed your pet for good"},
    {"RECIPES_25", "Completed 25 recipes"},
    {"QUESTS_COMPLETE_200", "Completed 200 quests"},
    {"QUESTS_COMPLETE_HATCH_50", "Completed 50 quests for Hatch"},
    {"QUESTS_COMPLETE_GAR_25", "Completed 25 quests for Gar"},
    {"GAMBLE_UNIQUE", "Won a unique item by gambling"},
    {"RETIRE_1", "Retired a character"},
    {"RETIRE_2", "Retired two characters"},
    {"RETIRE_300", "Retired 300 character levels in total"},
    {"MAX_FAME", "Reached the highest fame rank"},
    {"TRAVEL_25000", "Walked 25,000 steps"},
    {"DIE_500", "Died 500 times"},
    {"KILL_25_TROLL_CHMPS", "Killed 25 troll champions"},
    {"KILL_5000_MONSTERS", "Killed 5,000 monsters"},
    {"KILL_50000_MONSTERS", "Killed 50,000 monsters"},
    {"KILL_BRINK", "Defeated Brink"},
    {"KILL_LICH", "Defeated the Lich"},
    {"KILL_ROOT_GOLEM", "Defeated the Root Golem"},
    {"KILL_EMBER_COLOSSUS", "Defeated the Ember Colossus"},
    {"KILL_TROLL_BOSS", "Defeated the troll boss"},
    {"KILL_MEDEA", "Defeated Medea"},
    {"KILL_ALRIC", "Defeated Alric"},
    {"BEASTSLAYERI", "Defeated Ordrak"},
    {"BEASTSLAYERII", "Defeated Ordrak on Hard or Very Hard"},
    {"BEASTSLAYERIII", "Defeated Ordrak on Very Hard"},
    {"HARDCORE_VICTOR", "Defeated Ordrak in hardcore on Easy"},
    {"HARDCORE_HERO", "Defeated Ordrak in hardcore on Normal"},
    {"HARDCORE_CHAMPION", "Defeated Ordrak in hardcore on Hard"},
    {"HARDCORE_GOD", "Defeated Ordrak in hardcore on Very Hard"},
    {"SPEEDY", "Defeated Ordrak within 8 hours of play"},
    {"SPEED_KING", "Defeated Ordrak within 5 hours of play"},
    {"HAT_TRICK", "Won the game with all three classes"},
    {"DRINK_POTIONS", "Drank 5,000 potions"},
    {"SELL_ITEMS", "Sold 10,000 items"},
    {"HORSE_TALK", "Talked to the horse 100 times"},
    {"PERFECT_VICTORY", "Defeated Ordrak without ever dying"},
}};
// English text for `id`; the ID itself when it is not in the catalog.
constexpr std::string_view EnglishName(std::string_view id) {
  for (const auto& name : kDisplayNames)
    if (name.id == id) return name.english;
  return id;
}
} // namespace torchlight::achievements
