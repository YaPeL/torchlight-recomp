// Achievement/gameplay ABI, independent of renderer layouts. Reuse central endian helpers.
#pragma once
#include "guest_abi/ogre_layout.h"
#include "achievements/service.h"
#include <algorithm>
#include <cmath>
#include <optional>

namespace torchlight::guest_abi::achievements {
// A guest pointer worth following from the host. The hooks below read live game objects; during a
// level teardown a link can briefly hold a small value (a freed holder, an object being cleared), and
// a host read there lands on an unmapped guest page, which the runtime cannot resolve (it repeats the
// fault forever). The lowest 64 KiB are never a game object.
inline constexpr uint32_t kMinGuestObject = 0x10000;
inline bool Plausible(uint32_t address) { return address >= kMinGuestObject; }
// Evidence: guest 0x823D96F0 stores name +12, completion +40, selector +44, max +52.
inline constexpr uint32_t kAchievementName = 12;
// Unit-name comparisons in 0x822D7298 use the UTF-16BE string at unit +48.
inline constexpr uint32_t kUnitName = 48;
// PC 0x004D8DE0 +0x2C8 corresponds to guest +700: constructor 0x82283398,
// character state load 0x822A3368 (saved +256), and 0x821D3FD8 adds frame delta to it.
inline constexpr uint32_t kPlayedSeconds = 700;
// PC +0x754 corresponds to guest dispatcher 0x822D7520: +4*(5+456) = +1844.
inline constexpr uint32_t kDeaths = 1844;
// 0x822D7520 pocket-gold check reads +940. Hardcore read +2469 in the same routine.
inline constexpr uint32_t kGold = 940;
inline constexpr uint32_t kHardcore = 2469;
// Unit +96 points at a shared game context; repeated accesses in 0x821D3FD8 and
// 0x8234BE20 unwrap {array +20, count +24}. Context +5148 is set by SETDIFFICULTY
// and compared with 0..3 by DIFFICULTY at 0x8234CA14; PC counterpart is +0x243C.
inline constexpr uint32_t kContext = 96;
inline constexpr uint32_t kContextEligibility = 5124; // guest counterpart of PC +0x2428
inline constexpr uint32_t kContextDifficulty = 5148;
inline uint32_t Context(const uint8_t* base, uint32_t character) {
  if (!Plausible(character)) return 0;
  const auto shared = ReadU32(base, character + kContext);
  if (!Plausible(shared) || !ReadU32(base, shared + 24)) return 0;
  const auto array = ReadU32(base, shared + 20);
  if (!Plausible(array)) return 0;
  const auto context = ReadU32(base, array);
  return Plausible(context) ? context : 0;
}
// Global stat-manager 0x8355A294; 0x82217DA8 assigns current character at +88 before
// refreshing character stats through 0x823DC0E8. Do not use the stat vector as lifetime input.
inline uint32_t ActivePlayer(const uint8_t* base) {
  const auto manager = ReadU32(base, 0x8355A294);
  return manager ? ReadU32(base, manager + 88) : 0;
}
inline bool Eligible(const uint8_t* base, uint32_t character) {
  const auto context = Context(base, character);
  return context && ReadU32(base, context + kContextEligibility) == 1;
}
inline torchlight::achievements::Character Player(const uint8_t* base, uint32_t address) {
  torchlight::achievements::Character result;
  if (!address) return result;
  // Session-local identity only; account persistence never uses this address as a save identity.
  result.identity = std::to_string(address);
  const auto context = Context(base, address);
  if (context) {
    auto difficulty = ReadU32(base, context + kContextDifficulty);
    if (difficulty <= 3) result.difficulty = int(difficulty);
  }
  result.hardcore = ReadBool(base, address + kHardcore);
  result.played_seconds = ReadF32(base, address + kPlayedSeconds);
  result.deaths = int32_t(ReadU32(base, address + kDeaths));
  result.gold = int32_t(ReadU32(base, address + kGold));
  return result;
}
inline std::string UnitName(const uint8_t* base, uint32_t unit) {
  const auto str = unit + kUnitName;
  auto length = ReadU32(base, str + 16);
  if (length > 64) return {}; // boss identifiers only
  auto data = ReadU32(base, str + 20) >= 8 ? ReadU32(base, str) : str;
  if (!data) return {};
  std::string result;
  for (uint32_t i = 0; i < length; ++i) {
    const auto code = ReadU16(base, data + i*2);
    if (code > 127) return {};
    result.push_back(char(code));
  }
  return result;
}
// 0x822D7298 floors HP (+892) and proceeds when integer HP <=0. Keep that unusual
// predicate, rather than substituting a generic health<=0 condition.
inline bool BossDefeated(const uint8_t* base, uint32_t unit) {
  const float hp = ReadF32(base, unit + 892);
  return std::isfinite(hp) && std::floor(hp) <= 0;
}
// Deepest floor (STAT_DEEPEST_FLOOR, PC 0x004190EF / 0x0041A232): right after a level load PC
// compares level +0x140 of the game's current level (+0x38) with the stat and raises it.
// Guest level load 0x82217DA8 (PC 0x415820; both contain the FIRSTLEVEL completion, guest
// @0x822199F0, PC 0x417039) builds a new level (ctor 0x822E8E30 @0x82218640, which stores its
// depth argument r9 at +296) and stores it at game +56 (@0x82218654). The HUD's floor number is
// level +296 + 1 (next to L"Floor" @0x822197C0), so depth is zero-based like PC's (49/99).
inline constexpr uint32_t kLevelLoad = 0x82217DA8;
inline constexpr uint32_t kGameLevel = 56;
inline constexpr uint32_t kLevelDepth = 296;
// Return addresses of the game's level loads, the counterparts of PC's two update sites: game state
// update 0x82212950 (calls @0x82212F5C, depth game +4040, and @0x82213018) and floor transition
// 0x82214818 (@0x82215054: stairs = current depth + delta, explicit target, saved portal depth or
// 0 for town; the bottomless RANDOMDUNGEON uses this same path). The editor's load (0x82250B00
// @0x82250F9C, PC 0x4530E8) has no depth update in PC and is left out. The only other level
// creator, 0x82217248, always passes depth 0.
inline constexpr uint32_t kGameLevelLoadReturns[] = {0x82212F60, 0x8221301C, 0x82215058};
inline bool GameLevelLoad(uint32_t return_address) {
  for (auto r : kGameLevelLoadReturns) if (r == return_address) return true;
  return false;
}
// Zero-based depth of the game's current level, or nothing without one.
inline std::optional<int32_t> LevelDepth(const uint8_t* base, uint32_t game) {
  const auto level = game ? ReadU32(base, game + kGameLevel) : 0;
  if (!level) return std::nullopt;
  return int32_t(ReadU32(base, level + kLevelDepth));
}
// Items sold (STAT_TOTAL_ITEMS_SOLD, PC 0x004D667B): PC's player method 0x4D6670 only adds 1;
// its two callers (0x54A79A, 0x54AC31) are the merchant sale branches of one UI handler, each
// playing UI sound 0x17, paying the item's value (0x4B1780) through add-gold 0x4860B0 and then
// notifying. Guest sale handler 0x8236B7B8 does the same (sound 23 via 0x82377EE0, value
// 0x822AB950, add-gold 0x8229FB58 @0x8236B850, then moves the item to the merchant) without the
// notification; it is the guest's only call that plays the sale sound and pays an item's value.
// Not sales in PC either: the pet's town-sale loop 0x8228E580 (gold to the owner, no 0x4D6670),
// enchanting payments (0x82361E80, negative), gambling (0x8236B578) and quest rewards.
inline constexpr uint32_t kAddGold = 0x8229FB58;
inline constexpr uint32_t kItemSoldReturn = 0x8236B854;
// Exploding enemies (STAT_EXPLODE_ENEMY, PC 0x004A7D48): in PC's unit death 0x4A7570 the explosion
// block builds the effect (+0x60, +0xA5 = 1), calls 0x50E3F0, 0x5FB340 and 0x48A260(0, 1), the
// unit's +0x1AC object, sets unit +0x659 = 1 and adds 1. Guest unit death 0x8229D0C8 keeps the same
// straight-line block (effect +96/+165 = 1, 0x821D3F38, 0x822238E0 @0x8229D888, 0x821F0210(unit,
// 0, 1), unit +428 -> 0x822C36D8, unit +1605 = 1 @0x8229D8AC) without the add. The function's
// other call to 0x822238E0 (return 0x8229E198) is a different effect without those stores.
inline constexpr uint32_t kExplodeEffectReturn = 0x8229D88C;
// Potions given to a pet (STAT_POTIONS_PET, PC 0x004B50A8): per applied potion effect PC's
// 0x4B4FB0 checks item flag 33, sends character event 12 through 0x484660 (guest 0x82294548),
// dynamic-casts the target CBaseUnit -> CCharacter and adds 1 when the result's owner (+0x5B4) is
// set. Guest 0x822B9A60 sends event 12 (@0x822B9BB4, return 0x822B9BB8; the target stays in the
// preserved r28) and still calls __RTDynamicCast 0x821E1828 with the same type descriptors, but
// drops the result. PC +0x5B4 is the pet's owner: PC's pet town sale 0x4924E0 pays [pet+0x5B4],
// guest 0x8228E580 pays [pet+1444].
inline constexpr uint32_t kPotionEventReturn = 0x822B9BB8;
inline constexpr uint32_t kDynamicCast = 0x821E1828;
inline constexpr uint32_t kTypeBaseUnit = 0x834C27FC;  // .?AVCBaseUnit@@
inline constexpr uint32_t kTypeCharacter = 0x834C2814;  // .?AVCCharacter@@
inline constexpr uint32_t kCharacterOwner = 1444;
// The cast above, read from the host (the hooks call no guest code: see UnitIsType): MSVC RTTI,
// 32-bit (tools/guest_re/rtti_vtable.py). The object's vtable word -1 is its Complete Object
// Locator {signature +0, this vtable's offset in the object +4, ctor displacement +8, type
// descriptor +12, class hierarchy +16}; the hierarchy {signature, attributes, base count +8, base
// array +12} lists Base Class Descriptors {type descriptor +0, contained bases +4, member
// displacement +8, vbtable displacement +12 (-1: not virtual), offset in the vbtable +16,
// attributes +20}. The CCharacter base, when present and not virtual, is at the complete object +
// its member displacement; anything else (no such base, a virtual base, a malformed locator) gives 0,
// where __RTDynamicCast would give the pointer or 0.
inline uint32_t CastToCharacter(const uint8_t* base, uint32_t object) {
  if (!Plausible(object)) return 0;
  const auto vtable = ReadU32(base, object);
  if (!Plausible(vtable)) return 0;
  const auto locator = ReadU32(base, vtable - 4);
  if (!Plausible(locator) || ReadU32(base, locator) != 0) return 0;
  const auto complete = object - ReadU32(base, locator + 4);
  const auto hierarchy = ReadU32(base, locator + 16);
  if (!Plausible(complete) || !Plausible(hierarchy)) return 0;
  const auto count = ReadU32(base, hierarchy + 8);
  const auto bases = ReadU32(base, hierarchy + 12);
  if (!Plausible(bases) || count > 64) return 0;
  for (uint32_t i = 0; i < count; ++i) {
    const auto descriptor = ReadU32(base, bases + 4 * i);
    if (!Plausible(descriptor) || ReadU32(base, descriptor) != kTypeCharacter) continue;
    if (ReadU32(base, descriptor + 12) != 0xFFFFFFFFu) return 0;  // virtual base: not handled
    const auto character = complete + ReadU32(base, descriptor + 8);
    return Plausible(character) ? character : 0;
  }
  return 0;
}
// Levers (STAT_LEVERS_PULLED, PC 0x004DF4F2): PC's 0x4DF190 is CTriggerUnit's activation (vtable
// 0xA80284 slot 79; the only vtable that references it, so levers, plungers and horns share it).
// It ends with "if [this+0x278] == 0 and [this+0x1F4] == 1: add 1", then calls 0x4DE6C0(arg).
// Guest 0x822D8160 is the same method (CTriggerUnit vtable 0x82004894 slot 77) and ends the same
// way (container loop on +680 as PC +0x2A8, two frees, then 0x822D8548(this, arg) @0x822D853C,
// return 0x822D8540) without the check. The constructors (PC 0x4DDDAB, guest 0x822D7E78) lay out
// +0x1F0..+0x201 and +0x264..+0x288 at the same offsets:
// - +0x1F4 is the unit data's MAXSTATES - 1 (PC 0x4DF679 reads MAXSTATES with default 2 and
//   subtracts 1; 0x4DE6C0/0x822D8548 compare the state counter with it); guest +500.
// - +0x278 is the length of the std::wstring at +0x264, the unit data's SPAWNCLASS (PC 0x4E0B97
//   assigns it from that property; MSVC layout: allocator, 16-byte buffer, length +0x14, capacity
//   +0x18); the guest's string starts at +612 (buffer +0, length +16, capacity +20 = 7 set by the
//   constructor), so its length is +628.
// So PC counts every TRIGGER unit (factory 0x5FB529, L"TRIGGER") without SPAWNCLASS and with
// MAXSTATES 2: in this game's data levers, plungers, horns and the ballista lever, but also doors,
// gates, stairs, the mine entrance, portals, shrines, the fishing hole and bridges; chests and racks
// (SPAWNCLASS) and one-state portals do not (docs/achievement-coverage.md lists them).
inline constexpr uint32_t kActivationEndReturn = 0x822D8540;
inline constexpr uint32_t kTriggerActivationsNeeded = 500;
inline constexpr uint32_t kTriggerStringLength = 628;
inline bool LeverPulled(const uint8_t* base, uint32_t object) {
  return object && ReadU32(base, object + kTriggerStringLength) == 0 &&
         ReadU32(base, object + kTriggerActivationsNeeded) == 1;
}
// PC's stats flush points (stats manager 0x5F7980: upload, then the pending changes 0x5F72F0 are
// evaluated against the counter achievements; add/assign never evaluate, since no stat descriptor
// sets the immediate-upload flag +0x24, all cleared by the static initializers):
// - start of a level load: 0x41586B in 0x415820, guest level load kLevelLoad (on entry);
// - game state 2 in the state change 0x4188E0 (0x41899A, "cmp edi,2"), right before
//   0x5C6B50(&local); guest state change 0x82212950 compares r19 with 2 and calls 0x823A1280(&local)
//   at 0x82212AA8 (return kStateTwoFlushReturn) on the same +0x3C/+60 object;
// - loading the main menu level: 0x40ED0E at the start of 0x40ECE0 (mainmenu_townrules,
//   Title.ogg), guest kMenuLevelLoad (on entry);
// - quitting: 0x40A1F5 in the app frame 0x40A050 when the game asked to quit; the Xbox build quits by
//   launching the dashboard, so the host's close/shutdown paths stand for it.
inline constexpr uint32_t kMenuLevelLoad = 0x82217248;
inline constexpr uint32_t kStateTwoFlushCall = 0x823A1280;
inline constexpr uint32_t kStateTwoFlushReturn = 0x82212AAC;
// Maximum fame (MAX_FAME, PC 0x004A26BE): PC's rank-up 0x4A23B0 (a character virtual) adds to the
// fame rank at +0x3D0 and completes MAX_FAME when the rank == the FAMEGATE graph's largest number of
// control points (0x524E10 -> 0x5CC440 over the graph at +0xE8, loaded at 0x525DF2). The guest's
// rank-up 0x822A0570 (vtable slot 88) increments the rank at character +952 and instead sends
// event 6 when rank >= the number of fame titles - 1 (globals.dat TITLES, vector at +200 of
// *0x835594C4 filled by 0x8231D850): 34 titles, so rank 33, against PC's 55 FAMEGATE points (same
// data in both builds). Event 6 is only sent there and only means MAX_FAME. The guest's fame gain
// 0x822A0000 ranks up while fame reaches the gate and rank < the graph's maximum (0x82198E68), so the
// rank reaches the maximum and never passes it. The guest keeps the FAMEGATE graph at +200 of the
// object in global 0x835594CC (0x8231FF28 stores it, @0x82320674). 0x82198E68, the counterpart of
// PC 0x5CC440: over the graph's curves (pointer array +12, count +16) the largest
// (curve +120 - curve +116) / 8, the curve's control points.
inline constexpr uint32_t kFameRankUp = 0x822A0570;
inline constexpr uint32_t kFameRank = 952;
inline constexpr uint32_t kGraphsGlobal = 0x835594CC;
inline constexpr uint32_t kFameGateGraph = 200;
inline uint32_t MaxFameRank(const uint8_t* base) {
  const auto holder = ReadU32(base, kGraphsGlobal);
  const auto graph = holder ? ReadU32(base, holder + kFameGateGraph) : 0;
  if (!graph) return 0;
  const auto curves = ReadU32(base, graph + 12);
  const auto count = ReadU32(base, graph + 16);
  if (!curves || count > 64) return 0;
  uint32_t best = 0;
  for (uint32_t i = 0; i < count; ++i) {
    const auto curve = ReadU32(base, curves + 4 * i);
    if (!curve) continue;
    const auto begin = ReadU32(base, curve + 116), end = ReadU32(base, curve + 120);
    if (end >= begin) best = std::max(best, (end - begin) / 8);
  }
  return best;
}
// Unit types (guest 0x821D7CE0, PC 0x47EAF0, "has flag"): whether type id N is the unit's type
// (+372) or one of its ancestors. 0x821D7CE0 takes the type tree at unit +96 -> +16 and 0x821D7D38
// looks it up: equal ids are true at once; otherwise the tree holds a std::map {head +12} from type
// id to its ancestor ids, nodes {left +0, parent +4, right +8, key +12, vector {begin +16, end +20},
// nil flag +33}; lower_bound of the unit's type (the head's parent is the root), then the queried id
// among that entry's ancestors. Read here from the host (UnitIsType below): calling the guest
// function from a hook was the one thing the 2026-10-06 quit-to-menu hang needed.
inline constexpr uint32_t kUnitIsType = 0x821D7CE0;
inline constexpr uint32_t kUnitData = 96;
inline constexpr uint32_t kUnitDataTypeTree = 16;
inline constexpr uint32_t kUnitTypeId = 372;
inline bool UnitIsType(const uint8_t* base, uint32_t unit, uint32_t type) {
  if (!Plausible(unit)) return false;
  const auto data = ReadU32(base, unit + kUnitData);
  if (!Plausible(data)) return false;
  const auto tree = ReadU32(base, data + kUnitDataTypeTree);
  if (!Plausible(tree)) return false;
  const auto own = ReadU32(base, unit + kUnitTypeId);
  if (own == type) return true;
  const auto head = ReadU32(base, tree + 12);
  if (!Plausible(head)) return false;
  auto found = head;
  auto node = ReadU32(base, head + 4);
  for (int depth = 0; depth < 64 && Plausible(node) && !ReadU8(base, node + 33); ++depth) {
    if (ReadU32(base, node + 12) < own) {
      node = ReadU32(base, node + 8);
    } else {
      found = node;
      node = ReadU32(base, node + 0);
    }
  }
  if (found == head || own < ReadU32(base, found + 12)) return false;
  const auto begin = ReadU32(base, found + 16), end = ReadU32(base, found + 20);
  if (!Plausible(begin) || end < begin || end - begin > 4 * 1024) return false;
  for (uint32_t at = begin; at < end; at += 4) {
    if (ReadU32(base, at) == type) return true;
  }
  return false;
}
inline constexpr uint32_t kTypeSpell = 129;  // spell scrolls (data UNITTYPE SPELL)
inline constexpr uint32_t kTypePlayer = 28;  // 0x821D7E20: the type test with 28 (max-damage attacker)
inline constexpr uint32_t kTypePet = 87;     // pets: two spell slots instead of four (PC 0x482EB6)
// Unit subtype that PC's and the guest's pet item handlers reject (PC +0x284 == 41/42 in 0x58F5A1,
// guest +632 in GUIFEEDPET, command 68 of 0x82343EB0, and in 0x82367B58).
inline constexpr uint32_t kUnitSubtype = 632;
inline bool RejectedPetSubtype(const uint8_t* base, uint32_t unit) {
  const auto subtype = ReadU32(base, unit + kUnitSubtype);
  return subtype == 41 || subtype == 42;
}
// Teaching the pet a spell (PET_TRAINER, PC 0x005F50A2): PC's pet panel subscribes 0x58F530 to
// MouseButtonDown of its spell slots ("Spell" + N, 0x592A44); with a SPELL item (type 129) on the
// cursor and a pet whose subtype is not 41/42 it uses the item on the pet (0x549180) and sends event
// 31 without looking at the result. The guest keeps that handler as GUIFEEDPET (command 68 of
// 0x82343EB0, same subtype test, no event) but nothing sends it: on Xbox a pet learns from its own
// inventory, where using a spell (0x8236BD80, type 129 -> 0x822B9CA0 can-learn) runs 0x8236BB70,
// which uses the item with the pet as user and target through the item use 0x822B9A60 (return
// kPetSpellUseReturn). With both slots taken it opens the replace dialog instead (vtable 0x820D1AD4,
// stored at menu +380); the inventory's update 0x8236ECC0 then forgets the chosen slot (dialog +56,
// 0x82284FD0) and calls 0x8236BB70 again, which now uses the item at the same return; a cancelled
// dialog (+56 == -1) uses nothing.
inline constexpr uint32_t kItemUse = 0x822B9A60;
inline constexpr uint32_t kPetSpellUseReturn = 0x8236BC3C;
// The pet's main bag (BEAST_OF_BURDEN, PC 0x005F4DD7): the player's per-frame unit update 0x4D7C10
// (vtable slot 63, game state +0x2428 == 1, achievement not completed) takes the first pet
// (0x40E5F0(0): pets vector +0x5C4/+0x5C8), its inventory +0x404, and completes event 1 when the
// items in category 0 (0x4E5A30) equal that category's capacity (0x4E5E10). The guest's slot-63
// update 0x821DC980 (same FIDGET/IDLE strings) has no such check. Guest layout:
// - pets: vector {begin +1452, end +1456}; its elements point to a holder whose +0 is the pet (one
//   indirection more than PC, the same at every guest use: 0x82343EB0, 0x82367B58);
// - inventory: unit +1000 (0x82367B58 finds the player's items by GUID there);
// - inventory: total slots +24 (PC +0x18; the last category's end in 0x822E26D8), category ids
//   vector +64 (PC +0x48), category start slots vector {+80, +84} (PC +0x60/+0x64), items {data +32,
//   count +36} (PC +0x1C/+0x20), an item's slot at item +12 (PC +0xC); 0x822E1CD0 is the guest's
//   0x4E5A30 (count of a category's items) and the capacity below repeats 0x4E5E10;
// - category 0: PC asks for id 0 and 0x4E59D0 (like the guest's 0x822E1CD0) falls back to the
//   first category when the id is not listed. The pet's inventory lists a single category (id 5,
//   slots 19..69 of 69: capacity 50, the pet's 50-slot bag; 2026-10-06 run), so it is the bag.
inline constexpr uint32_t kPetsBegin = 1452;
inline constexpr uint32_t kPetsEnd = 1456;
inline constexpr uint32_t kInventory = 1000;
inline constexpr uint32_t kInventoryTotalSlots = 24;
inline constexpr uint32_t kInventoryCategoryIds = 64;
inline constexpr uint32_t kInventoryCategoryStartsBegin = 80;
inline constexpr uint32_t kInventoryCategoryStartsEnd = 84;
inline constexpr uint32_t kInventoryItems = 32;
inline constexpr uint32_t kInventoryItemCount = 36;
inline constexpr uint32_t kItemSlot = 12;
inline constexpr uint32_t kBagCategory = 0;
inline uint32_t FirstPet(const uint8_t* base, uint32_t unit) {
  if (!Plausible(unit)) return 0;
  const auto begin = ReadU32(base, unit + kPetsBegin), end = ReadU32(base, unit + kPetsEnd);
  if (!Plausible(begin) || end <= begin) return 0;
  const auto holder = ReadU32(base, begin);
  if (!Plausible(holder)) return 0;
  const auto pet = ReadU32(base, holder);
  return Plausible(pet) ? pet : 0;
}
// Items in a category and its capacity (0x4E5A30 / 0x4E5E10 over the guest layout); nothing for a
// malformed inventory.
struct CategoryFill {
  uint32_t items = 0;
  uint32_t capacity = 0;
};
inline std::optional<CategoryFill> InventoryCategoryFill(const uint8_t* base, uint32_t inventory,
                                                         uint32_t category) {
  if (!Plausible(inventory)) return std::nullopt;
  const auto starts = ReadU32(base, inventory + kInventoryCategoryStartsBegin);
  const auto starts_end = ReadU32(base, inventory + kInventoryCategoryStartsEnd);
  const auto ids = ReadU32(base, inventory + kInventoryCategoryIds);
  if (!Plausible(starts) || !Plausible(ids) || starts_end <= starts || (starts_end - starts) / 4 > 64)
    return std::nullopt;
  const uint32_t count = (starts_end - starts) / 4;
  // As 0x4E59D0 / 0x822E1CD0: the category's index, the first category when its id is not listed.
  uint32_t index = 0;
  for (uint32_t i = count; i-- > 0;) {
    if (ReadU32(base, ids + 4 * i) == category) { index = i; break; }
  }
  const auto start = ReadU32(base, starts + 4 * index);
  const auto end = index + 1 == count ? ReadU32(base, inventory + kInventoryTotalSlots)
                                      : ReadU32(base, starts + 4 * (index + 1));
  if (end < start) return std::nullopt;
  CategoryFill fill;
  fill.capacity = end - start;
  const auto items = ReadU32(base, inventory + kInventoryItems);
  const auto item_count = ReadU32(base, inventory + kInventoryItemCount);
  if (item_count && !Plausible(items)) return std::nullopt;
  if (item_count > 1024) return std::nullopt;
  for (uint32_t i = 0; i < item_count; ++i) {
    const auto item = ReadU32(base, items + 4 * i);
    if (!Plausible(item)) continue;
    const auto slot = ReadU32(base, item + kItemSlot);
    if (slot >= start && (index + 1 == count || slot < end)) ++fill.items;
  }
  return fill;
}
} // namespace torchlight::guest_abi::achievements
