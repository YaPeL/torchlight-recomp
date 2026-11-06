// The game's NETBOOK MODE from the host (settings::HostSettings::low_quality). The game keeps it in
// its settings table, which it fills from its first-start defaults at boot (its files cannot be
// read before SAVE:\ is mounted) and again from local_settings.txt each time the main menu is
// entered. So after each of those, the host sets the value it chose through the game's own setter
// (guest_abi game_settings), off as well as on. The game also saves the table to local_settings.txt;
// while the mode is on, it is set to 0 for that save and back to 1 after it, so the save keeps the
// 360's value and does not carry the mode to another installation (or the PC version). The calls
// run on the game's thread, at the entry or after the return of the hooked function (GuestCall).

#include <cstdint>

#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include "game_menu/guest_call.h"
#include "guest_abi/guest_functions.h"
#include "live/install.h"

namespace {

namespace abi = torchlight::guest_abi;
namespace gs = torchlight::guest_abi::functions::game_settings;

#define FUNCTION_ADDRESS_CHECK(entry, addr) \
  static_assert(gs::entry.address == 0x##addr##u, "guest_functions mismatch")

bool logged = false;

bool SettingsTable(const uint8_t* base, uint32_t table) {
  return table && abi::ReadU32(base, table) == gs::kTableVtable;  // not another table's load/save
}

bool LowQuality() { return torchlight::live::StartupHostSettings().low_quality; }

bool SetNetbookMode(PPCContext& ctx, uint8_t* base, uint32_t table, bool on) {
  const uint32_t index = abi::ReadU32(base, gs::kNetbookModeIndex);
  torchlight::game_menu::GuestCall call(ctx, base);
  return call.Call(gs::kSetInt.address, {table, index, on ? 1u : 0u}) != 0;
}

void SetLowQuality(PPCContext& ctx, uint8_t* base, uint32_t table) {
  if (!SettingsTable(base, table)) return;
  const bool low = LowQuality();
  const uint32_t index = abi::ReadU32(base, gs::kNetbookModeIndex);
  if (!SetNetbookMode(ctx, base, table, low)) {
    REXLOG_ERROR("settings: the game has no setting {} for its low quality mode", index);
  } else if (!logged) {
    REXLOG_INFO("settings: low quality (netbook mode) {}", low ? "on" : "off");
    logged = true;
  }
}

}  // namespace

extern "C" {

FUNCTION_ADDRESS_CHECK(kLoad, 823981C8);
REX_EXTERN(__imp__sub_823981C8);
REX_FUNC(sub_823981C8) {
  const uint32_t table = ctx.r3.u32;
  __imp__sub_823981C8(ctx, base);
  SetLowQuality(ctx, base, table);
}

FUNCTION_ADDRESS_CHECK(kSave, 82397C28);
REX_EXTERN(__imp__sub_82397C28);
REX_FUNC(sub_82397C28) {
  const uint32_t table = ctx.r3.u32;
  const bool hide = SettingsTable(base, table) && LowQuality();
  if (hide) SetNetbookMode(ctx, base, table, false);
  __imp__sub_82397C28(ctx, base);
  if (hide) SetNetbookMode(ctx, base, table, true);
}

FUNCTION_ADDRESS_CHECK(kFirstStartDefaults, 823E1040);
REX_EXTERN(__imp__sub_823E1040);
REX_FUNC(sub_823E1040) {
  const uint32_t table = ctx.r3.u32;
  __imp__sub_823E1040(ctx, base);
  SetLowQuality(ctx, base, table);
}

}  // extern "C"
