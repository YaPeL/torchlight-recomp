// The game's NETBOOK MODE from the host (settings::HostSettings::low_quality). The game keeps it in
// its settings table, which it fills from its first-start defaults at boot (its files cannot be
// read before SAVE:\ is mounted) and again from local_settings.txt each time the main menu is
// entered; it also saves the table, so a value forced once would stay in the save. So after each of
// those, the host sets the value it chose through the game's own setter (guest_abi game_settings),
// off as well as on: the save then always holds the host's choice. The calls run after the hooked
// function returned (GuestCall), on the game's thread.

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

void SetLowQuality(PPCContext& ctx, uint8_t* base, uint32_t table) {
  if (!table || abi::ReadU32(base, table) != gs::kTableVtable) return;  // another table's load
  const bool low = torchlight::live::StartupHostSettings().low_quality;
  const uint32_t index = abi::ReadU32(base, gs::kNetbookModeIndex);
  torchlight::game_menu::GuestCall call(ctx, base);
  if (call.Call(gs::kSetInt.address, {table, index, low ? 1u : 0u}) == 0) {
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

FUNCTION_ADDRESS_CHECK(kFirstStartDefaults, 823E1040);
REX_EXTERN(__imp__sub_823E1040);
REX_FUNC(sub_823E1040) {
  const uint32_t table = ctx.r3.u32;
  __imp__sub_823E1040(ctx, base);
  SetLowQuality(ctx, base, table);
}

}  // extern "C"
