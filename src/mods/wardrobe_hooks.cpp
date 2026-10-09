// A character's wardrobe is not built on a body model without an entity (wardrobe_guard.h,
// guest_abi/wardrobe.h): the game's first build reads the entity without testing it, and when the
// game could not make one (a mod left the model's mesh name empty, "/.mesh") that read faults at
// 0xB4 and the game repeats it forever. The build returns as it does when there is nothing to build,
// and the log says which model and, when the wardrobe is built while a unit is made, which unit.

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_set>

#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include "guest_abi/mods.h"
#include "guest_abi/ogre_layout.h"
#include "guest_abi/wardrobe.h"
#include "mods/wardrobe_guard.h"

namespace {

namespace abi = torchlight::guest_abi;
namespace wardrobe = torchlight::guest_abi::wardrobe;
using abi::ReadU16;
using abi::ReadU32;

static_assert(wardrobe::kBuild.address == 0x822DC6B0);
static_assert(wardrobe::kMakeUnit.address == 0x823DE8F0);

// The unit being made on this thread (kMakeUnit), for the log; empty outside one.
thread_local std::string t_unit;

std::string Ascii(char16_t c) { return std::string(1, c < 0x80 ? char(c) : '?'); }

// A UTF-16 string ending in 0, as kMakeUnit takes the name.
std::string ReadZString(const uint8_t* base, uint32_t at) {
  std::string out;
  for (uint32_t i = 0; at && i < 256; ++i) {
    const uint16_t c = ReadU16(base, at + 2 * i);
    if (!c) break;
    out += Ascii(c);
  }
  return out;
}

// A guest std::wstring (the model's mesh name).
std::string ReadWString(const uint8_t* base, uint32_t str) {
  const uint32_t length = ReadU32(base, str + abi::ogre::stl_string::kLength.offset);
  const uint32_t capacity = ReadU32(base, str + abi::ogre::stl_string::kCapacity.offset);
  if (length > 512) return "?";
  const uint32_t text = capacity > abi::mods::manager::kWStringInlineCapacity ? ReadU32(base, str) : str;
  std::string out;
  for (uint32_t i = 0; i < length; ++i) out += Ascii(ReadU16(base, text + 2 * i));
  return out;
}

// Each model is reported once: the game asks for the build again on later updates.
bool FirstReport(uint32_t model) {
  static std::mutex mutex;
  static std::unordered_set<uint32_t> reported;
  std::lock_guard lock(mutex);
  return reported.insert(model).second;
}

}  // namespace

REX_EXTERN(__imp__sub_822DC6B0);
REX_EXTERN(__imp__sub_823DE8F0);

extern "C" {

REX_FUNC(sub_823DE8F0) {
  std::string outer = std::move(t_unit);
  t_unit = ReadZString(base, ctx.r4.u32);
  __imp__sub_823DE8F0(ctx, base);
  t_unit = std::move(outer);
}

REX_FUNC(sub_822DC6B0) {
  const uint32_t w = ctx.r3.u32;
  torchlight::mods::WardrobeFields fields;
  if (w) {
    fields.rebuild = ReadU32(base, w + wardrobe::layout::kRebuild.offset);
    fields.wardrobe_entity = ReadU32(base, w + wardrobe::layout::kWardrobeEntity.offset);
    fields.node = ReadU32(base, w + wardrobe::layout::kNode.offset);
    fields.model = ReadU32(base, w + wardrobe::layout::kModel.offset);
    if (fields.model) fields.model_entity = ReadU32(base, fields.model + wardrobe::model::kEntity.offset);
  }
  if (w && torchlight::mods::SkipWardrobeBuild(fields)) {
    if (FirstReport(fields.model)) {
      const std::string mesh =
          fields.model ? ReadWString(base, fields.model + wardrobe::model::kMeshName.offset) : "(no model)";
      REXLOG_WARN("mods: wardrobe not built: model 0x{:08X} has no entity (mesh \"{}\"){}", fields.model, mesh,
                  t_unit.empty() ? std::string() : " while making unit \"" + t_unit + "\"");
    }
    return;
  }
  __imp__sub_822DC6B0(ctx, base);
}

}  // extern "C"
