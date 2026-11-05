// The guest OGRE's pixel colour packing (Ogre::PixelUtil, OGRE 1.7 OgrePixelFormat.cpp), for the
// host versions of its 8-bit pack and unpack (hooks/pixel_util.h), with their evidence.

#pragma once

#include <cstdint>

#include "guest_abi/guest_functions.h"
#include "guest_abi/ogre_layout.h"

namespace torchlight::guest_abi::pixel_util {

using functions::GuestFunction;

// [confirmed] PixelUtil::unpackColour(uint8* r, uint8* g, uint8* b, uint8* a, PixelFormat pf,
// const void* src): r3..r6 the four outputs, r7 the format, r8 the pixel. Its description is
// kFormatTable + 44 * pf (mulli @0x82474570). A format with kNativeEndian (rlwinm @0x82474590):
//  - the pixel read by kReadPixel with the format's element size (+4, lbz @0x824745A0);
//  - kLuminance (@0x824745A8): r = g = b = fixedToFixed((value & rmask) >> rshift, rbits, 8),
//    stored to *b, *g, *r (@0x824745D0..@0x824745D8);
//  - else r, g, b each from its own mask, shift and bits (@0x824745E8..@0x8247462C);
//  - kHasAlpha (clrlwi @0x82474634): a from amask, ashift, abits (@0x82474640..@0x8247465C), else
//    255 (@0x82474668).
// Without kNativeEndian it unpacks to floats (sub_824746D8, @0x82474688) and converts each with
// floatToFixed(f, 8) (sub_82473320). Its only writes are the four output bytes.
inline constexpr GuestFunction kUnpackColour8{0x82474560, Confidence::kConfirmed};

// [confirmed] PixelUtil::packColour(uint8 r, uint8 g, uint8 b, uint8 a, PixelFormat pf,
// void* dest): r3..r6 the channels (clrlwi to 8 bits), r7 the format, r8 the pixel. With
// kNativeEndian (@0x82473FD0): value = (fixedToFixed(r, 8, rbits) << rshift & rmask) | the same for
// g, b, a (@0x82473FE0..@0x82474064), written by the element size (@0x82474070): 1 byte, a
// big-endian halfword, three bytes high first (@0x824740A4..@0x824740B4), or a big-endian word;
// any other size writes nothing. Without it, each channel times 1/255 (pool 0x820AA2C0 + 92) to the
// float packColour (sub_82474148, @0x8247413C). Its only writes are the pixel's bytes.
inline constexpr GuestFunction kPackColour8{0x82473FA0, Confidence::kConfirmed};

// [confirmed] The float versions the two call for the other formats; not replaced.
inline constexpr GuestFunction kUnpackColourFloat{0x824746D8, Confidence::kConfirmed};
inline constexpr GuestFunction kPackColourFloat{0x82474148, Confidence::kConfirmed};

// [confirmed] Bitwise::fixedToFixed(value, n, p) (sub_824732C8, called by both with n, p in r4, r5)
// and Bitwise::floatToFixed (sub_82473320): OgreBitwise.h, the same code.
inline constexpr GuestFunction kFixedToFixed{0x824732C8, Confidence::kConfirmed};
// [confirmed] The read of 1 to 4 big-endian bytes by element size (sub_82473388): 0 for any other.
inline constexpr GuestFunction kReadPixel{0x82473388, Confidence::kConfirmed};

// [confirmed] The pixel format database (OgrePixelFormat.cpp _pixelFormats[PF_COUNT]): lis -31994,
// addi 24240 in both functions; 42 entries of 44 bytes, PF_UNKNOWN to PF_PVRTC_RGBA4, each name
// pointer reading as in the OGRE 1.7 table (checked in the image).
inline constexpr uint32_t kFormatTable = 0x83065EB0;
inline constexpr uint32_t kFormatCount = 42;
inline constexpr uint32_t kFormatSize = 44;

// PixelFormatDescription (OgrePixelFormat.cpp), the fields both functions read.
namespace format {
inline constexpr Field kElemBytes{4, Confidence::kConfirmed};    // u8
inline constexpr Field kFlags{8, Confidence::kConfirmed};        // u32
inline constexpr Field kBits{17, Confidence::kConfirmed};        // u8 r, g, b, a
inline constexpr Field kMasks{24, Confidence::kConfirmed};       // u32 r, g, b, a
inline constexpr Field kShifts{40, Confidence::kConfirmed};      // u8 r, g, b, a
}  // namespace format

// PixelFormatFlags (OgrePixelFormat.h).
inline constexpr uint32_t kHasAlpha = 0x01;
inline constexpr uint32_t kNativeEndian = 0x10;
inline constexpr uint32_t kLuminance = 0x20;

}  // namespace torchlight::guest_abi::pixel_util
