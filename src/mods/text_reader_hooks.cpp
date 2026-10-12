// PC text data files for the game's text reader. PC writes text files as UTF-16LE (a mod's .DAT);
// the Xbox reader takes UTF-16 big-endian, as the Xbox game writes its own (guest_abi mods.h,
// text_reader). After the reader loads a file, one that is clearly little-endian
// (text_encoding.h) has its units turned around in the guest's buffer, so it reads as on PC; the
// file on disk is not touched, the game's own files, big-endian, go through as they are, and a
// file whose order is unclear is left as it is and logged. The reader's position needs no
// change: a PC mark (FF FE) reads as the 0xFFFE it already skips.

#include <cstdint>
#include <mutex>
#include <span>
#include <string>
#include <unordered_set>

#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include "guest_abi/mods.h"
#include "mods/text_encoding.h"

namespace {

namespace reader_abi = torchlight::guest_abi::mods::text_reader;
using torchlight::guest_abi::ReadU32;

static_assert(reader_abi::kLoadText.address == 0x82399C00);

constexpr uint32_t kMaxUnits = 64u << 20;  // a count past this is not a text file's

// The game's name for the file (a guest std::wstring), for the log; non-ASCII as '?'.
std::string ReadWStringUtf8(const uint8_t* base, uint32_t str) {
  namespace ogre = torchlight::guest_abi::ogre;
  const uint32_t length = ReadU32(base, str + ogre::stl_string::kLength.offset);
  const uint32_t capacity = ReadU32(base, str + ogre::stl_string::kCapacity.offset);
  if (length > 1024) return "?";
  const uint32_t text =
      capacity > torchlight::guest_abi::mods::manager::kWStringInlineCapacity ? ReadU32(base, str) : str;
  std::string out;
  for (uint32_t i = 0; i < length; ++i) {
    const uint16_t c = torchlight::guest_abi::ReadU16(base, text + 2 * i);
    out += c < 0x80 ? char(c) : '?';
  }
  return out;
}

// Each file is logged once: the game reads a base definition again for every unit built on it.
bool FirstTime(const std::string& path) {
  static std::mutex mutex;
  static std::unordered_set<std::string> logged;
  std::lock_guard lock(mutex);
  return logged.insert(path).second;
}

}  // namespace

REX_EXTERN(__imp__sub_82399C00);

extern "C" {

REX_FUNC(sub_82399C00) {
  const uint32_t reader = ctx.r3.u32;
  const std::string path = ReadWStringUtf8(base, ctx.r4.u32);
  __imp__sub_82399C00(ctx, base);
  if (!ctx.r3.u32) return;
  const uint32_t buffer = ReadU32(base, reader + reader_abi::kBuffer.offset);
  const uint32_t count = ReadU32(base, reader + reader_abi::kCount.offset);
  if (!buffer || count < 2 || count > kMaxUnits) return;
  const std::span<uint8_t> text(base + buffer, size_t(count - 1) * 2);  // the last unit is the 0 added
  switch (torchlight::mods::DetectUtf16Order(text)) {
    case torchlight::mods::Utf16Order::kBig:
      return;
    case torchlight::mods::Utf16Order::kLittle:
      torchlight::mods::SwapUtf16Units(text);
      if (FirstTime(path)) {
        REXLOG_INFO("mods: text file {} read as little-endian UTF-16 ({} units turned around)", path, count - 1);
      }
      return;
    case torchlight::mods::Utf16Order::kUnclear:
      if (FirstTime(path)) REXLOG_WARN("mods: text file {}: byte order unclear, read as it is", path);
      return;
  }
}

}  // extern "C"
