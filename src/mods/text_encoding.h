// The encoding of a mod's text data files as the game's text reader gets them (docs/mods.md,
// section 7e). PC's reader (0x5C1499, 0x5C173A) takes a file's bytes as UTF-16 in the machine's
// order, little-endian, and skips a first unit of 0xFEFF: PC text files are UTF-16LE, with or
// without a byte order mark, and nothing else (UTF-8 or ANSI read as garbage there too). The Xbox
// reader is the same code on a big-endian machine, and the Xbox game writes its own text files as
// the bytes FF FE followed by big-endian units (settings.txt: FF FE 00 43 ...); a PC file reads as
// garbage there, so the host turns such a file's units around in the guest's buffer. No guest,
// OGRE or platform types.

#pragma once

#include <cstdint>
#include <span>

namespace torchlight::mods {

enum class Utf16Order {
  kBig,      // the Xbox's: leave it
  kLittle,   // PC's: turn it around
  kUnclear,  // neither clearly: leave it (and say so)
};

// How many units after a leading FF FE (either machine's mark) are looked at.
inline constexpr size_t kUtf16SampleUnits = 64;

// The byte order of text the game reads as UTF-16, judged on up to kUtf16SampleUnits units after
// an FF FE mark: a unit below U+0100 (the text data files are almost all ASCII) has one zero byte,
// the first in big-endian and the second in little-endian; units with no zero byte or two say
// nothing. The order is the one at least 90% of the telling units show, with at least 4 of them;
// anything else, a reversed mark (FE FF) or a file too short, is kUnclear, except that FE FF
// followed by a clear big-endian sample is kBig.
Utf16Order DetectUtf16Order(std::span<const uint8_t> bytes);

// Turns every whole 16-bit unit of `bytes` around (a trailing odd byte is left as it is).
void SwapUtf16Units(std::span<uint8_t> bytes);

}  // namespace torchlight::mods
