// Language packs: translations the game does not pick by itself (guest_abi/game_ui.h
// kNativeTranslationLanguages), in the user's translations/<code>/ folder. The game's "Translate
// File" key gets that folder's file as its default (resourceconfig.dat, which could override it, is
// never there), and the game loads it like its own translations. Any mode; set up by
// live::ApplyLanguage before the guest runs.

#include <cstring>
#include <string>

#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include "game_menu/guest_call.h"
#include "game_menu/text_conversion.h"
#include "guest_abi/game_ui.h"
#include "guest_abi/ogre_layout.h"
#include "live/install.h"

namespace ui = torchlight::guest_abi::game_ui;
namespace abi = torchlight::guest_abi;

namespace torchlight::game_menu {

namespace {

void WriteU32(uint8_t* base, uint32_t addr, uint32_t value) {
  base[addr] = uint8_t(value >> 24);
  base[addr + 1] = uint8_t(value >> 16);
  base[addr + 2] = uint8_t(value >> 8);
  base[addr + 3] = uint8_t(value);
}

// A guest std::wstring's text (UTF-16, big-endian in guest memory). Same layout as the narrow one
// (ogre_layout.h stl_string) in 16-bit units: inline below capacity 8 (kWStringFromText).
std::u16string ReadWString(const uint8_t* base, uint32_t str, uint32_t max_length = 1024) {
  const uint32_t length = abi::ReadU32(base, str + abi::ogre::stl_string::kLength.offset);
  const uint32_t capacity = abi::ReadU32(base, str + abi::ogre::stl_string::kCapacity.offset);
  if (length > max_length) return {};
  const uint32_t data = capacity >= 8 ? abi::ReadU32(base, str) : str;
  std::u16string text(length, u'\0');
  for (uint32_t i = 0; i < length; ++i) text[i] = char16_t(abi::ReadU16(base, data + 2 * i));
  return text;
}

// Text as a NUL-terminated big-endian UTF-16 block on the guest heap.
uint32_t AllocWideText(GuestCall& call, const std::string& ascii) {
  const uint32_t block = call.Call(ui::kAlloc.address, {0, uint32_t(ascii.size() + 1) * 2});
  if (!block) return 0;
  for (size_t i = 0; i <= ascii.size(); ++i) {
    const uint16_t c = i < ascii.size() ? uint8_t(ascii[i]) : 0;
    call.base()[block + 2 * i] = uint8_t(c >> 8);
    call.base()[block + 2 * i + 1] = uint8_t(c);
  }
  return block;
}

}  // namespace

}  // namespace torchlight::game_menu

#define FUNCTION_ADDRESS_CHECK(entry, addr) \
  static_assert(ui::entry.address == 0x##addr##u, "game_ui mismatch")

extern "C" {

FUNCTION_ADDRESS_CHECK(kConfigRegisterString, 82398588);
REX_EXTERN(__imp__sub_82398588);
REX_FUNC(sub_82398588) {
  using namespace torchlight::game_menu;
  const std::string pack = torchlight::live::LanguagePack();
  if (pack.empty() || ReadWString(base, ctx.r4.u32) != ui::kTranslateFileKey) {
    __imp__sub_82398588(ctx, base);
    return;
  }
  // The default the game passes is replaced by the pack's file; built on the guest heap (the
  // callee's own frame takes the stack below ours) and freed after.
  const std::string path = "translations\\" + pack + "\\translation.dat";
  uint32_t text = 0, value = 0;
  {
    GuestCall call(ctx, base);
    text = AllocWideText(call, path);
    value = text ? call.Call(ui::kAlloc.address, {0, abi::ogre::stl_string::kSize.bytes}) : 0;
    if (value) call.Call(ui::kWStringFromText.address, {value, text});
  }
  if (!value) {
    REXLOG_ERROR("language pack: cannot build the translation path; the game's default stays");
    __imp__sub_82398588(ctx, base);
    return;
  }
  ctx.r5.u64 = value;
  __imp__sub_82398588(ctx, base);
  const uint64_t result = ctx.r3.u64;
  {
    GuestCall call(ctx, base);
    call.Call(ui::kWStringDtor.address, {value});
    call.Call(ui::kFree.address, {value});
    call.Call(ui::kFree.address, {text});
  }
  ctx.r3.u64 = result;
  REXLOG_INFO("language pack: Translate File = {}", path);
}

// The game's wstring -> CEGUI::String conversion keeps 8 bits per character; this one keeps the
// whole code point (text_conversion.h), and otherwise does what it does: the String starts empty,
// grows through the game's own function, and gets the text and its terminator.
FUNCTION_ADDRESS_CHECK(kWStringToCeguiString, 82345898);
REX_FUNC(sub_82345898) {
  using namespace torchlight::game_menu;
  namespace cs = ui::cegui_string;
  const uint32_t str = ctx.r3.u32;
  const std::vector<uint32_t> text =
      Utf16ToCodePoints(ReadWString(base, ctx.r4.u32, 1u << 20));
  for (uint32_t field : {cs::kLength.offset, cs::kEncodedBufferLength.offset,
                         cs::kEncodedDataLength.offset, cs::kEncodedBuffer.offset,
                         cs::kInlineBuffer.offset, cs::kHeapBuffer.offset}) {
    WriteU32(base, str + field, 0);
  }
  WriteU32(base, str + cs::kReserve.offset, cs::kInlineCapacity);
  {
    GuestCall call(ctx, base);
    call.Call(ui::kCeguiStringGrow.address, {str, uint32_t(text.size())});
  }
  const uint32_t reserve = abi::ReadU32(base, str + cs::kReserve.offset);
  const uint32_t buffer = reserve > cs::kInlineCapacity
                              ? abi::ReadU32(base, str + cs::kHeapBuffer.offset)
                              : str + cs::kInlineBuffer.offset;
  WriteU32(base, str + cs::kLength.offset, uint32_t(text.size()));
  for (size_t i = 0; i < text.size(); ++i) WriteU32(base, buffer + uint32_t(4 * i), text[i]);
  WriteU32(base, buffer + uint32_t(4 * text.size()), 0);
  ctx.r3.u64 = str;
}

}  // extern "C"
