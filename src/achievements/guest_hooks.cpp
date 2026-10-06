// Strong overrides of generated weak guest functions, preserving gameplay and guest ABI.
#include <atomic>
#include <cstdio>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include "achievements/runtime.h"
#include "dev/guest_command.h"
#include "guest_abi/achievements.h"
namespace pc = torchlight::achievements;
namespace abi = torchlight::guest_abi::achievements;
namespace {
// Levers follow PC's trigger condition (guest_abi kActivationEndReturn), which also counts doors
// and stairs. Pet potions count when a potion is used on the pet: on Xbox from the pet's own
// inventory (the inventory menu uses an item with its owner as user and target), the path below; a
// transfer to the pet's inventory is not a use (2026-10-05 run). The agreed diagnostic reports
// potions from this path that do not count.
constexpr bool kCountLevers = true;
constexpr bool kCountPetPotions = true;
pc::Observation Observe(std::string_view source,const PPCContext& ctx,const uint8_t* base,
                        uint32_t actor=0,int64_t event=-1,int64_t value=0,std::string_view id={}) {
  if (!pc::NativeEnabled() || !pc::DiagnosticsEnabled()) return {};
  if (!actor) actor=abi::ActivePlayer(base);
  return {source,uint32_t(ctx.lr),actor,event,value,id,
      actor?std::optional<bool>(abi::Eligible(base,actor)):std::nullopt,abi::Player(base,actor)};
}
}
extern "C" {
REX_EXTERN(__imp__sub_821EF318);
REX_FUNC(sub_821EF318) {
  __imp__sub_821EF318(ctx,base);
  if (pc::NativeEnabled()) pc::Pump();
}

REX_EXTERN(__imp__sub_823D9930);
REX_FUNC(sub_823D9930) {
  std::string id;
  if (pc::NativeEnabled() && ctx.r3.u32)
    id = torchlight::guest_abi::ogre::ReadString(base,ctx.r3.u32+abi::kAchievementName,64);
  const auto observation=Observe("retained-completion",ctx,base,0,-1,0,id);
  __imp__sub_823D9930(ctx,base);
  if (pc::QualifiedGuestCompletion(id)) pc::Apply([&](pc::Service& service) { service.Unlock(id); },observation);
}
REX_EXTERN(__imp__sub_82375500);
REX_FUNC(sub_82375500) {
  if (!pc::NativeEnabled()) { __imp__sub_82375500(ctx,base); return; }
  const auto event = ctx.r3.u32;
  const auto* definition=pc::Event(event);
  if (definition && pc::QualifiedGuestCompletion(definition->id))
    pc::Apply([&](pc::Service& service) { service.Explicit(event); },
        Observe("xbox-mapper",ctx,base,0,event,0,definition->id));
  // Suppress queue creation, so no Xbox overlapped request exists to be completed/polled.
  ctx.r3.u64 = 0;
}
REX_EXTERN(__imp__sub_822D7298);
REX_FUNC(sub_822D7298) {
  const bool native = pc::NativeEnabled();
  const auto player = ctx.r3.u32, unit = ctx.r4.u32;
  pc::Character inputs;
  std::string boss;
  if (native && player && unit && abi::BossDefeated(base,unit)) {
    inputs = abi::Player(base,player);
    boss = abi::UnitName(base,unit);
  }
  const auto observation=Observe("boss-completion",ctx,base,player,-1,0,boss);
  __imp__sub_822D7298(ctx,base);
  // Normal bosses already reach the mapper override. Only restore the absent ORDRAK block.
  if (boss == "ORDRAK") pc::Apply([&](pc::Service& service) {
    service.Bind(inputs); service.Boss(boss);
  },observation);
  else if (!observation.source.empty()) pc::Apply([](pc::Service&){},observation);
}
REX_EXTERN(__imp__sub_8287D8C0);
REX_FUNC(sub_8287D8C0) {
  // The game's "Achievements" button (XamShowAchievementsUI, an SDK stub): our list of the set in
  // use opens instead, on the UI thread; the call returns success at once, as on the console.
  if (pc::ShowAchievementList()) ctx.r3.u64 = 0;
  else if (!pc::NativeEnabled()) __imp__sub_8287D8C0(ctx,base);
  else ctx.r3.u64 = 0; // no Xbox achievement UI in PC mode
}
REX_EXTERN(__imp__sub_822D7520);
REX_FUNC(sub_822D7520) {
  if (pc::NativeEnabled()) {
    const auto character = ctx.r3.u32, event = ctx.r4.u32;
    const auto delta = ctx.r5.s32;
    const auto observation=Observe("character-event",ctx,base,character,event,delta);
    if (character && abi::Eligible(base,character)) {
      auto inputs = abi::Player(base,character);
      pc::Apply([&](pc::Service& service) {
        service.Bind(inputs); service.CharacterEvent(event,delta,true);
      },observation);
    } else if (!observation.source.empty()) pc::Apply([](pc::Service&){},observation);
  }
  __imp__sub_822D7520(ctx,base);
}
REX_EXTERN(__imp__sub_82361C78);
REX_FUNC(sub_82361C78) {
  const auto player = pc::NativeEnabled() ? abi::ActivePlayer(base) : 0;
  const bool eligible = player && abi::Eligible(base,player);
  const auto observation=Observe("enchant-attempt",ctx,base,player);
  __imp__sub_82361C78(ctx,base);
  // Original return is the enchant attempt's success flag; type-4 early exit returns 1.
  // PC 0x00577768 adds one failure independently of prior item enchant count.
  if (eligible && !ctx.r3.u8) pc::Apply([](pc::Service& service) { service.Add(7,1); },observation);
}
REX_EXTERN(__imp__sub_82361B00);
REX_FUNC(sub_82361B00) {
  // PC retirement has a different dialog; guest accepted path sets retired +2468 before
  // requesting a return to menu, so capture inputs before the original can release player.
  uint32_t player = 0;
  int32_t level = 0;
  bool eligible = false;
  if (pc::NativeEnabled() && ctx.r3.u32 && ctx.r4.u32) {
    const auto ui = ctx.r3.u32;
    if (torchlight::guest_abi::ReadU32(base,ui+300) == 4) {
      const auto game = torchlight::guest_abi::ReadU32(base,ui+168);
      player = game ? torchlight::guest_abi::ReadU32(base,game+56) : 0;
      eligible = player && abi::Eligible(base,player) &&
          !torchlight::guest_abi::ReadBool(base,player+2468);
      if (player) level = int32_t(torchlight::guest_abi::ReadU32(base,player+240));
    }
  }
  const auto observation=Observe("retirement",ctx,base,player,-1,level);
  __imp__sub_82361B00(ctx,base);
  if (eligible && ctx.r3.u8) pc::Apply([&](pc::Service& service) {
    service.Add(11,1); service.Add(12,level);
  },observation);
}
REX_EXTERN(__imp__sub_8221EF28);
REX_FUNC(sub_8221EF28) {
  // Restore removed PC stat updates at the existing qualifying name-comparison boundary.
  // Exact return addresses select gameplay call sites, never renderer/material names.
  const uint32_t caller = uint32_t(ctx.lr);
  const bool relevant = caller == 0x82356904 || caller == 0x82355E90 || caller == 0x82355EAC ||
      caller == 0x823CF6A8 || caller == 0x823CF710 || caller == 0x823CF778 ||
      caller == 0x8229FE60 || caller == 0x8229FE7C;
  const auto player = relevant && pc::NativeEnabled() ? abi::ActivePlayer(base) : 0;
  const bool eligible = player && abi::Eligible(base,player);
  const auto observation=eligible?Observe("qualified-name",ctx,base,player):pc::Observation{};
  __imp__sub_8221EF28(ctx,base);
  if (!eligible || !ctx.r3.u8) return;
  pc::Apply([&](pc::Service& service) {
    switch (caller) {
      case 0x8229FE60: case 0x8229FE7C: service.Add(19,1); break; // dead Troll/Troll Juggernaut champion
      case 0x82356904: service.Add(27,1); break; // Horse interaction; PC 0x00598B8F
      case 0x82355E90: service.Add(25,1); break; // completed quest, Male1 (Hatch)
      case 0x82355EAC: service.Add(26,1); break; // completed quest, Gar
      case 0x823CF6A8: service.ClassWin(23); break; // Destroyer
      case 0x823CF710: service.ClassWin(22); break; // Alchemist
      case 0x823CF778: service.ClassWin(21); break; // Vanquisher
    }
  },observation);
}

REX_EXTERN(__imp__sub_822A0570);
REX_FUNC(sub_822A0570) {
  // Fame rank-up: PC completes MAX_FAME when the new rank equals the FAMEGATE maximum (guest_abi
  // kFameRankUp); the guest's own event 6 (last title) is not accepted in native mode.
  const auto character = ctx.r3.u32;
  __imp__sub_822A0570(ctx,base);
  if (!pc::NativeEnabled() || !character) return;
  const auto rank = torchlight::guest_abi::ReadU32(base,character+abi::kFameRank);
  const auto max = abi::MaxFameRank(base);
  auto observation = Observe("fame-rank",ctx,base,character,-1,rank);
  if (max && rank == max) pc::Apply([](pc::Service& service) { service.Unlock("MAX_FAME"); },observation);
  else if (!observation.source.empty()) pc::Apply([](pc::Service&){},observation);
}

REX_EXTERN(__imp__sub_822D8548);
REX_FUNC(sub_822D8548) {
  // At the end of an object activation PC counts a pulled lever before this call (guest_abi
  // kActivationEndReturn), from the object's state at that point.
  if (kCountLevers && pc::NativeEnabled() && uint32_t(ctx.lr) == abi::kActivationEndReturn &&
      abi::LeverPulled(base,ctx.r3.u32)) {
    const auto player = abi::ActivePlayer(base);
    auto observation = Observe("lever-pulled",ctx,base,player,-1,1);
    if (player && abi::Eligible(base,player))
      pc::Apply([](pc::Service& service) { service.Add(14,1); },observation);
    else if (!observation.source.empty()) pc::Apply([](pc::Service&){},observation);
  }
  __imp__sub_822D8548(ctx,base);
}

REX_EXTERN(__imp__sub_82294548);
REX_FUNC(sub_82294548) {
  // Character event of a unit; from a potion effect (guest_abi kPotionEventReturn) PC then counts a
  // potion given to a pet: the target, still in the caller's preserved r28, cast to CCharacter
  // (guest_abi CastToCharacter), with an owner.
  const auto from = uint32_t(ctx.lr);
  __imp__sub_82294548(ctx,base);
  if (!pc::NativeEnabled() || from != abi::kPotionEventReturn) return;
  const auto target = ctx.r28.u32;
  uint32_t character = 0, owner = 0;
  character = abi::CastToCharacter(base,target);  // the guest's cast, read from the host
  if (character) owner = torchlight::guest_abi::ReadU32(base,character+abi::kCharacterOwner);
  const auto player = abi::ActivePlayer(base);
  const bool eligible = player && abi::Eligible(base,player);
  if (kCountPetPotions && character && owner && eligible) {
    auto observation = Observe("pet-potion",ctx,base,player,-1,1);
    observation.caller = from;
    pc::Apply([](pc::Service& service) { service.Add(20,1); },observation);
  } else if (pc::DiagnosticsEnabled()) {
    // Agreed diagnostic (2026-10-05): what this path sees for a potion that is not counted.
    char detail[96];
    std::snprintf(detail,sizeof(detail),"target=0x%08X character=0x%08X owner=0x%08X",
                  target,character,owner);
    auto observation = Observe("pet-potion-check",ctx,base,player,-1,0,detail);
    observation.caller = from;
    pc::Apply([](pc::Service&){},observation);
  }
}

REX_EXTERN(__imp__sub_822B9A60);
REX_FUNC(sub_822B9A60) {
  // Item use. From the pet inventory's spell learning (guest_abi kPetSpellUseReturn, also reached
  // after the replace dialog picks a slot) it is the use PC's pet spell slot makes before sending
  // PET_TRAINER, with PC's conditions: a spell, used by a pet whose subtype is not 41/42; counted
  // whatever the use does, like PC.
  const auto from = uint32_t(ctx.lr);
  const auto item = ctx.r3.u32, user = ctx.r4.u32;
  const bool qualifies = pc::NativeEnabled() && from == abi::kPetSpellUseReturn &&
      abi::UnitIsType(base,item,abi::kTypeSpell) && abi::UnitIsType(base,user,abi::kTypePet) &&
      !abi::RejectedPetSubtype(base,user);
  __imp__sub_822B9A60(ctx,base);
  if (!qualifies) return;
  const auto player = abi::ActivePlayer(base);
  auto observation = Observe("pet-spell",ctx,base,player,-1,1);
  observation.caller = from;
  pc::Apply([](pc::Service& service) { service.Unlock("PET_TRAINER"); },observation);
}

REX_EXTERN(__imp__sub_821DC980);
REX_FUNC(sub_821DC980) {
  // A character's per-frame update; PC's counterpart checks whether its pet's bag is full
  // (guest_abi FirstPet, InventoryCategoryFill). Cheap until then: no pet or no PC set returns at
  // once, and once the achievement is in the state nothing is read again.
  static std::atomic<bool> done{false};
  const auto unit = ctx.r3.u32;
  __imp__sub_821DC980(ctx,base);
  if (done.load(std::memory_order_relaxed) || !pc::NativeEnabled() || !unit) return;
  const auto pet = abi::FirstPet(base,unit);
  if (!pet || !abi::Eligible(base,unit)) return;
  const auto bag = torchlight::guest_abi::ReadU32(base,pet+abi::kInventory);
  const auto fill = abi::InventoryCategoryFill(base,bag,abi::kBagCategory);
  if (!fill || !fill->capacity || fill->items != fill->capacity) return;
  if (pc::Unlocked("BEAST_OF_BURDEN")) { done = true; return; }
  auto observation = Observe("pet-bag-full",ctx,base,unit,-1,fill->items);
  pc::Apply([](pc::Service& service) { service.Unlock("BEAST_OF_BURDEN"); },observation);
  done = pc::Unlocked("BEAST_OF_BURDEN");
}

REX_EXTERN(__imp__sub_822238E0);
REX_FUNC(sub_822238E0) {
  // From the explosion block of a unit's death it is PC's exploding-enemy add (guest_abi
  // kExplodeEffectReturn); the block runs straight on to the unit's exploded flag.
  const auto from = uint32_t(ctx.lr);
  __imp__sub_822238E0(ctx,base);
  if (!pc::NativeEnabled() || from != abi::kExplodeEffectReturn) return;
  const auto player = abi::ActivePlayer(base);
  auto observation = Observe("enemy-exploded",ctx,base,player,-1,1);
  observation.caller = from;
  if (player && abi::Eligible(base,player))
    pc::Apply([](pc::Service& service) { service.Add(24,1); },observation);
  else if (!observation.source.empty()) pc::Apply([](pc::Service&){},observation);
}

REX_EXTERN(__imp__sub_8229FB58);
REX_FUNC(sub_8229FB58) {
  // Add-gold; from the merchant sale it is also PC's item-sold notification (guest_abi
  // kItemSoldReturn). The gold itself reaches the character-event hook as before.
  const auto from = uint32_t(ctx.lr);
  __imp__sub_8229FB58(ctx,base);
  if (!pc::NativeEnabled() || from != abi::kItemSoldReturn) return;
  const auto player = abi::ActivePlayer(base);
  auto observation = Observe("item-sold",ctx,base,player,-1,1);
  observation.caller = from;
  if (player && abi::Eligible(base,player))
    pc::Apply([](pc::Service& service) { service.Add(17,1); },observation);
  else if (!observation.source.empty()) pc::Apply([](pc::Service&){},observation);
}

REX_EXTERN(__imp__sub_82217248);
REX_FUNC(sub_82217248) {
  // Main menu level load: one of PC's stats flush points (guest_abi kMenuLevelLoad).
  if (pc::NativeEnabled()) pc::FlushCounters(Observe("counter-flush",ctx,base));
  __imp__sub_82217248(ctx,base);
}
REX_EXTERN(__imp__sub_823A1280);
REX_FUNC(sub_823A1280) {
  // Entering game state 2: PC flushes its stats right before this call (guest_abi
  // kStateTwoFlushReturn).
  if (pc::NativeEnabled() && uint32_t(ctx.lr) == abi::kStateTwoFlushReturn)
    pc::FlushCounters(Observe("counter-flush",ctx,base));
  __imp__sub_823A1280(ctx,base);
}

REX_EXTERN(__imp__sub_82217DA8);
REX_FUNC(sub_82217DA8) {
  // The start of every level load is a PC stats flush point (guest_abi kLevelLoad); after the
  // game's loads PC raises STAT_DEEPEST_FLOOR to the new level's depth through its
  // eligibility-gated stat assignment (kGameLevelLoadReturns), evaluated at the next flush.
  const auto game = ctx.r3.u32;
  const auto from = uint32_t(ctx.lr);
  if (pc::NativeEnabled()) pc::FlushCounters(Observe("counter-flush",ctx,base));
  __imp__sub_82217DA8(ctx,base);
  if (abi::GameLevelLoad(from)) torchlight::dev::OnGameLevelLoaded();
  if (!pc::NativeEnabled() || !abi::GameLevelLoad(from)) return;
  const auto depth = abi::LevelDepth(base,game);
  if (!depth) return;
  const auto player = abi::ActivePlayer(base);
  auto observation = Observe("level-depth",ctx,base,player,-1,*depth);
  observation.caller = from;
  if (player && abi::Eligible(base,player))
    pc::Apply([&](pc::Service& service) { service.Maximum(5,*depth); },observation);
  else if (!observation.source.empty()) pc::Apply([](pc::Service&){},observation);
}

REX_EXTERN(__imp__sub_821D7E78);
REX_FUNC(sub_821D7E78) {
  // PC max-damage block is just before health modification. Guest damage application
  // 0x8229C7B0 reaches this health setter at 0x8229CAD0 after the same early exits;
  // preserved nonvolatile r29 = attacker and f30 = actual applied damage.
  if (pc::NativeEnabled() && uint32_t(ctx.lr) == 0x8229CAD4 && ctx.r29.u32) {
    const auto player = abi::ActivePlayer(base);
    const double damage = ctx.f30.f64;
    if (player && abi::Eligible(base,player) && std::isfinite(damage) &&
        damage > 0 && damage <= double(INT32_MAX)) {
      // 0x821D7E20 is the type test with type 28 (the player), read from the host.
      if (abi::UnitIsType(base,ctx.r29.u32,abi::kTypePlayer)) pc::Apply([&](pc::Service& service) {
        service.Maximum(3,int32_t(damage));
      },Observe("applied-damage",ctx,base,player,-1,int32_t(damage)));
    }
  }
  __imp__sub_821D7E78(ctx,base);
}

}
