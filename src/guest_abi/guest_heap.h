// The guest heap behind guest_abi game_ui kAlloc / kFree (nedmalloc over dlmalloc, the C runtime's
// heap the game's operator new and OGRE's allocations use): the chunk layout kFree reads, and the
// allocator's functions. Used by the GUI redraw filter's shadow mode (diagnostics builds) to tell
// the allocator's own writes and dead memory from the rest.

#pragma once

#include <cstdint>

namespace torchlight::guest_abi::guest_heap {

// [confirmed] A block's dlmalloc chunk, as kFree (sub_821CD9A8) reads it: the size word just below
// the block (`lwz r11,-4(r29)` @0x821CDA34), the chunk in use when its bit 1 is set
// (`rlwinm r10,r11,0,30,30` @0x821CDA38), the size its bits above 2 (`rlwinm r11,r11,0,0,28`
// @0x821CDA44), of which the block has 8 bytes less (`addi r5,r11,-8` @0x821CDA48): the chunk
// starts 8 bytes below the block.
inline constexpr uint32_t kSizeWordBelow = 4;
inline constexpr uint32_t kChunkInUse = 2;
inline constexpr uint32_t kChunkSizeMask = ~7u;
inline constexpr uint32_t kChunkHeaderBytes = 8;

// [confirmed] kAlloc's call that allocates a renderer quad node returns to 0x821D39B0 (sub_821D3990:
// `li r4,92` @0x821D39A4, then `bl 0x821cd7f8`); the thread cache serves it from its 128-byte bin.
inline constexpr uint32_t kQuadNodeAllocReturn = 0x821D39B0;

// [confirmed] The allocator's functions: the ones kAlloc and kFree reach (their calls followed four
// deep), less the C runtime's shared ones (critical sections, abort). Among them nedmalloc's thread
// cache, threadcache_malloc sub_8241F6F8 (its counters at +8, +16, +20, its bins from +24, a popped
// block's neighbour's link) and threadcache_free sub_8241F820 (its counter at +12, the freed block's
// first 16 bytes, its bins); dlmalloc's mspace_malloc sub_8241EEC0 and tmalloc_large sub_8241DC98
// (the treebins from the mspace's +304). They write only the allocator's own state and the headers
// and links of chunks.
inline constexpr uint32_t kAllocatorFunctions[] = {
    0x821CD7F8, 0x821CD9A8, 0x821CCF30, 0x8241C870, 0x8241D308, 0x8241DC98, 0x8241E228,
    0x8241EBE0, 0x8241ED08, 0x8241EDC0, 0x8241EEC0, 0x8241F310, 0x8241F3B0, 0x8241F450,
    0x8241F4C0, 0x8241F6F8, 0x8241F7D8, 0x8241F820, 0x8241F8F8, 0x8241FAB8,
};

}  // namespace torchlight::guest_abi::guest_heap
