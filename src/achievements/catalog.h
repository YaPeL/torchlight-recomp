// Factual catalog: PC 0x005F4C30, guest 0x823D9BB8; docs/achievements.md.
#pragma once
#include <array>
#include <cstdint>
#include <string_view>
namespace torchlight::achievements {
struct Definition { std::string_view id; uint8_t stat; int32_t threshold; int8_t event; };
inline constexpr std::array<Definition, 66> kCatalog{{
    {"TORCHLIGHT_ACHIEVEMENT_FIRSTLEVEL", 30, 1, 0},
    {"BREAKABLES", 1, 1500, -1},
    {"BEAST_OF_BURDEN", 30, 1, 1},
    {"PLAYER_GOLD_COLLECTED", 13, 250000, -1},
    {"PLAYER_GOLD_IN_POCKET", 30, 100000, 35},
    {"CRITICAL_STRIKE_ON_DEATH", 24, 25, -1},
    {"MAX_DMG_DONE", 3, 10000, -1},
    {"REACH_LVL_50", 5, 49, -1},
    {"REACH_LVL_100", 5, 99, -1},
    {"PET_TRAINER", 30, 1, 31},
    {"PET_MIMIC", 30, 1, 30},
    {"ENCHANTER_SUCCESS_5", 30, 1, 32},
    {"ENCHANTER_SUCCESS_10", 30, 1, 33},
    {"ENCHANTER_FAILURE_FIRST", 30, 1, 29},
    {"ENCHANTER_FAILURE_20", 7, 20, -1},
    {"GAMBLER_20", 9, 20, -1},
    {"GAMBLER_50", 9, 50, -1},
    {"GAMBLER_100", 9, 100, -1},
    {"MODS_1", 30, 1, 26},
    {"MODS_5", 30, 1, 27},
    {"MODS_10", 30, 1, 28},
    {"PLAYER_LEVEL_65", 30, 1, 24},
    {"PLAYER_LEVEL_100", 30, 1, 25},
    {"PULLED_LEVERS_100", 14, 100, -1},
    {"PET_POTIONS_50", 20, 50, -1},
    {"PET_SEND_TO_TOWN", 30, 1, 2},
    {"FISH_CAUGHT_50", 6, 50, -1},
    {"FISH_CAUGHT_100", 6, 100, -1},
    {"FISH_CAUGHT_1000", 6, 1000, -1},
    {"PET_FEED_FISH_ANY", 30, 1, 3},
    {"PET_FEED_FISH_PERMANENT", 30, 1, 4},
    {"RECIPES_25", 8, 25, -1},
    {"QUESTS_COMPLETE_200", 10, 200, -1},
    {"QUESTS_COMPLETE_HATCH_50", 25, 50, -1},
    {"QUESTS_COMPLETE_GAR_25", 26, 25, -1},
    {"GAMBLE_UNIQUE", 30, 1, 5},
    {"RETIRE_1", 11, 1, -1},
    {"RETIRE_2", 11, 2, -1},
    {"RETIRE_300", 12, 300, -1},
    {"MAX_FAME", 30, 1, 6},
    {"TRAVEL_25000", 15, 25000, -1},
    {"DIE_500", 0, 500, -1},
    {"KILL_25_TROLL_CHMPS", 19, 25, -1},
    {"KILL_5000_MONSTERS", 4, 5000, -1},
    {"KILL_50000_MONSTERS", 4, 50000, -1},
    {"KILL_BRINK", 30, 1, 7},
    {"KILL_LICH", 30, 1, 8},
    {"KILL_ROOT_GOLEM", 30, 1, 9},
    {"KILL_EMBER_COLOSSUS", 30, 1, 10},
    {"KILL_TROLL_BOSS", 30, 1, 11},
    {"KILL_MEDEA", 30, 1, 12},
    {"KILL_ALRIC", 30, 1, 13},
    {"BEASTSLAYERI", 30, 1, 14},
    {"BEASTSLAYERII", 30, 1, 15},
    {"BEASTSLAYERIII", 30, 1, 16},
    {"HARDCORE_VICTOR", 30, 1, 17},
    {"HARDCORE_HERO", 30, 1, 18},
    {"HARDCORE_CHAMPION", 30, 1, 19},
    {"HARDCORE_GOD", 30, 1, 20},
    {"SPEEDY", 30, 1, 21},
    {"SPEED_KING", 30, 1, 22},
    {"HAT_TRICK", 30, 1, 23},
    {"DRINK_POTIONS", 16, 5000, -1},
    {"SELL_ITEMS", 17, 10000, -1},
    {"HORSE_TALK", 27, 100, -1},
    {"PERFECT_VICTORY", 30, 1, 34},
}};
inline constexpr std::array<std::string_view, 31> kStatKeys{{
    "STAT_DEATHS",
    "STAT_BREAKABLES",
    "STAT_CRITICAL_STRIKES",
    "STAT_MAX_DMG_DONE",
    "STAT_MONSTERS_KILLED",
    "STAT_DEEPEST_FLOOR",
    "STAT_FISH_CAUGHT",
    "STAT_ENCHANTER_FAILS",
    "STAT_RECIPES_MADE",
    "STAT_GAMBLE_COUNT",
    "STAT_QUESTS_COMPLETED",
    "STAT_RETIRED_COUNT",
    "STAT_RETIRED_LVLS_TOTAL",
    "STAT_GOLD_COLLECTED",
    "STAT_LEVERS_PULLED",
    "STAT_TOTAL_STEPS",
    "STAT_TOTAL_POTIONS_USED",
    "STAT_TOTAL_ITEMS_SOLD",
    "STAT_DEATHS_HARDCORE",
    "STAT_TROLL_CHMPS",
    "STAT_POTIONS_PET",
    "STAT_WIN_VANQ",
    "STAT_WIN_ALCH",
    "STAT_WIN_DESTROYER",
    "STAT_EXPLODE_ENEMY",
    "STAT_QUESTS_COMPLETED_HATCH",
    "STAT_QUESTS_COMPLETED_GARR",
    "STAT_HORSE_TALK",
    "PLAYER_DEATHS",
    "PLAYER_GOLD",
    "NONE",
}};
constexpr const Definition* Find(std::string_view id) {
  for (const auto& d : kCatalog) if (d.id == id) return &d;
  return nullptr;
}
constexpr const Definition* Event(uint32_t event) {
  if (event >= 36) return nullptr;
  for (const auto& d : kCatalog) if (d.event == int(event)) return &d;
  return nullptr;
}
// The guest's own completions of BEAST_OF_BURDEN and PET_TRAINER are not PC's checks (the guest
// never sends them; native mode restores PC's checks in guest_hooks.cpp: the pet's full bag and a
// spell taught to the pet). MODS_1/5/10 are accepted: the guest's own check (global data loader,
// events 26..28) is PC's, and the host now gives it the player's mods (docs/mods.md).
// Direct service inputs remain available for isolated rule tests.
// MAX_FAME is excluded too: the guest's event 6 fires at the last fame title (rank 33), while PC
// requires the FAMEGATE maximum; native mode restores PC's check at the rank-up (guest_hooks.cpp).
constexpr bool QualifiedGuestCompletion(std::string_view id) {
  return Find(id) && id!="BEAST_OF_BURDEN" && id!="PET_TRAINER" &&
      id!="MAX_FAME";
}
} // namespace torchlight::achievements
