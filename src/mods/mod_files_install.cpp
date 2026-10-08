#include "mods/mod_files_install.h"

#include <optional>
#include <set>
#include <string>

#include <rex/logging.h>
#include <rex/ppc/func.h>

#include "game_menu/guest_call.h"
#include "guest_abi/game_ui.h"
#include "guest_abi/mods.h"
#include "mods/mod_files.h"
#include "save_import/pak.h"

REX_EXTERN(__imp__sub_823AA988);

namespace torchlight::mods {

namespace {

namespace abi = torchlight::guest_abi;
namespace ui = torchlight::guest_abi::game_ui;
namespace mods_abi = torchlight::guest_abi::mods;
using game_menu::GuestCall;

std::optional<ModFileTable> g_table;
std::set<std::u16string> g_logged;

std::u16string ReadWString(const uint8_t* base, uint32_t str) {
  const uint32_t length = abi::ReadU32(base, str + abi::ogre::stl_string::kLength.offset);
  const uint32_t capacity = abi::ReadU32(base, str + abi::ogre::stl_string::kCapacity.offset);
  if (length > 0xFFFF) return {};
  const uint32_t text = capacity > mods_abi::manager::kWStringInlineCapacity ? abi::ReadU32(base, str) : str;
  std::u16string out(length, u'\0');
  for (uint32_t i = 0; i < length; ++i) out[i] = static_cast<char16_t>(abi::ReadU16(base, text + 2 * i));
  return out;
}

}  // namespace

void InstallModFiles(const std::filesystem::path& mods_folder, const ModPlan* plan) {
  g_table.reset();
  g_logged.clear();
  if (!plan) return;
  g_table = ModFileTable::Scan(mods_folder, *plan);
  REXLOG_INFO("mods: {} files in the enabled mods", g_table->size());
}

namespace {

// guest_abi/mods.h kModFileLookup: the original first; when no mod has the request as spelled, and
// one has it under another case or separators, the original again with that spelling.
void HookModFileLookup(PPCContext& ctx, uint8_t* base) {
  const uint32_t result = ctx.r3.u32, manager = ctx.r4.u32, request = ctx.r5.u32;
  const uint32_t out6 = ctx.r6.u32, out7 = ctx.r7.u32;
  __imp__sub_823AA988(ctx, base);
  if (!g_table || abi::ReadU32(base, result + abi::ogre::stl_string::kLength.offset) != 0) return;
  const std::u16string asked = ReadWString(base, request);
  const auto spelling = g_table->Spelling(asked);
  if (!spelling || *spelling == asked) return;

  // The spelling as a guest std::wstring on the heap (the scratch area is below the stack pointer
  // the original will use).
  uint32_t text = 0, str = 0;
  {
    GuestCall call(ctx, base);
    text = call.Call(mods_abi::kAlloc.address, {0, static_cast<uint32_t>(2 * (spelling->size() + 1))});
    str = call.Call(mods_abi::kAlloc.address, {0, abi::ogre::stl_string::kSize.bytes});
    if (!text || !str) return;
    for (size_t i = 0; i <= spelling->size(); ++i) {
      const char16_t c = i < spelling->size() ? (*spelling)[i] : u'\0';
      base[text + 2 * i] = static_cast<uint8_t>(c >> 8);
      base[text + 2 * i + 1] = static_cast<uint8_t>(c);
    }
    call.Call(ui::kWStringFromText.address, {str, text});
  }
  ctx.r3.u64 = result;
  ctx.r4.u64 = manager;
  ctx.r5.u64 = str;
  ctx.r6.u64 = out6;
  ctx.r7.u64 = out7;
  __imp__sub_823AA988(ctx, base);
  const bool found = abi::ReadU32(base, result + abi::ogre::stl_string::kLength.offset) != 0;
  {
    GuestCall call(ctx, base);
    call.Call(ui::kWStringDtor.address, {str});
    call.Call(mods_abi::kFree.address, {str});
    call.Call(mods_abi::kFree.address, {text});
  }
  ctx.r3.u64 = result;  // the original returns its result string
  if (g_logged.size() < 100 && g_logged.insert(NormalizeModPath(asked)).second) {
    REXLOG_INFO("mods: {} asked as {}: {}", save_import::Utf8(*spelling), save_import::Utf8(asked),
                found ? "found" : "not found");
  }
}

}  // namespace

}  // namespace torchlight::mods

extern "C" {
static_assert(torchlight::guest_abi::mods::kModFileLookup.address == 0x823AA988u, "mods mismatch");
REX_FUNC(sub_823AA988) { torchlight::mods::HookModFileLookup(ctx, base); }
}
