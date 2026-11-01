// The guest's hottest tree walks as host code (guest_abi functions::kDeviceMapFind, kTreeNext17,
// kTreeNext21): literal translations of their instructions, each listed beside its line, leaving the
// same registers (r8-r11; cr6 is a local of the generated code) and making the same stores, the red
// zone's included. The node offsets are the instructions' own (MSVC trees: left +0, parent +4,
// right +8, key +12, isnil +17 or +21), so they stay here with them rather than in guest_abi.
// The walks take where their stores go: guest memory (the hooks, guest_tree_hooks.cpp) or a record
// (the check, and the tests over a fake address space, guest_tree_test.cpp).

#pragma once

#include <cstdint>
#include <iterator>
#include <optional>
#include <utility>

#include <rex/ppc/context.h>

#include "guest_abi/ogre_layout.h"

namespace torchlight::hooks::guest_tree {

inline uint32_t R32(const uint8_t* base, uint32_t address) {
  return guest_abi::ReadU32(base, address);
}
inline uint32_t R8(const uint8_t* base, uint32_t address) {
  return guest_abi::ReadU8(base, address);
}

// Where the host code's stores go: guest memory, or a record for the check.
struct MemoryStores {
  uint8_t* base;
  void operator()(uint32_t address, uint32_t value) const {
    guest_abi::WriteU32(base, address, value);
  }
};
struct RecordedStores {
  struct Store {
    uint32_t address, value;
  };
  Store stores[16];
  uint32_t count = 0;
  bool overflow = false;
  void operator()(uint32_t address, uint32_t value) {
    for (uint32_t i = 0; i < count; ++i) {
      if (stores[i].address == address) {  // the last value per address (an iterator walking up)
        stores[i].value = value;
        return;
      }
    }
    if (count == std::size(stores)) {
      overflow = true;
      return;
    }
    stores[count++] = {address, value};
  }
};

// The registers a walk leaves; unset ones are left as they were.
struct Registers {
  std::optional<uint32_t> r8, r9, r10, r11;
  void Apply(PPCContext& ctx) const {
    if (r8) ctx.r8.u64 = *r8;
    if (r9) ctx.r9.u64 = *r9;
    if (r10) ctx.r10.u64 = *r10;
    if (r11) ctx.r11.u64 = *r11;
  }
};

// 0x82581D98: std::map<uint32_t, T>::find (MSVC tree: left +0, parent +4, right +8, key +12, isnil
// +21) into *r3, for the map r4 and the key at r5.
template <typename Stores>
Registers DeviceMapFind(const PPCContext& ctx, const uint8_t* base, Stores&& store) {
  Registers out;
  const uint32_t r1 = ctx.r1.u32, r3 = ctx.r3.u32, r4 = ctx.r4.u32, r5 = ctx.r5.u32;
  uint32_t r10 = R32(base, r4 + 4);               // lwz r10,4(r4): the head
  uint32_t r11 = R32(base, r10 + 4);              // lwz r11,4(r10): the root
  uint32_t r9 = R8(base, r11 + 21);               // lbz r9,21(r11)
  if (r9 == 0) {                                  // bne cr6,0x82581dd8
    r9 = R32(base, r5);                           // lwz r9,0(r5): the key
    uint32_t r8;
    do {
      r8 = R32(base, r11 + 12);                   // lwz r8,12(r11)
      if (r8 < r9) {                              // cmplw; bge cr6,0x82581dc4
        r11 = R32(base, r11 + 8);                 // lwz r11,8(r11)
      } else {
        r10 = r11;                                // mr r10,r11
        r11 = R32(base, r11);                     // lwz r11,0(r11)
      }
      r8 = R8(base, r11 + 21);                    // lbz r8,21(r11)
    } while (r8 == 0);                            // beq cr6,0x82581db0
    out.r8 = r8;
  }
  r11 = R32(base, r4 + 4);                        // lwz r11,4(r4)
  store(r1 - 16, r10);                            // stw r10,-16(r1)
  if (r10 != r11) {                               // beq cr6,0x82581e08
    r9 = R32(base, r5);                           // lwz r9,0(r5)
    const uint32_t r8 = R32(base, r10 + 12);      // lwz r8,12(r10)
    out.r8 = r8;
    if (!(r9 < r8)) {                             // blt cr6,0x82581e08
      r11 = r10;                                  // addi r11,r1,-16; lwz r11,0(r11)
      store(r3, r11);                             // stw r11,0(r3)
      out.r9 = r9, out.r10 = r10, out.r11 = r11;
      return out;
    }
  }
  store(r1 - 12, r11);                            // stw r11,-12(r1); addi; lwz r11,0(r11)
  store(r3, r11);                                 // stw r11,0(r3)
  out.r9 = r9, out.r10 = r10, out.r11 = r11;
  return out;
}

// 0x824C6960 (isnil +17) and 0x8262D6C8 (isnil +21): a tree iterator's operator++ on the iterator
// at r3 (MSVC tree: left +0, parent +4, right +8).
template <uint32_t kIsNil, typename Stores>
Registers TreeNext(const PPCContext& ctx, const uint8_t* base, Stores&& store) {
  Registers out;
  const uint32_t r3 = ctx.r3.u32;
  // The iterator's value as the walk leaves it: the guest reloads it after its own stores, which
  // the check's recorded stores do not make.
  uint32_t at_r3 = R32(base, r3);
  uint32_t r11 = at_r3;                           // lwz r11,0(r3)
  uint32_t r10 = R8(base, r11 + kIsNil);          // lbz r10,N(r11)
  out.r10 = r10, out.r11 = r11;
  if (r10 != 0) return out;                       // bnelr cr6
  r10 = R32(base, r11 + 8);                       // lwz r10,8(r11): the right child
  uint32_t r9 = R8(base, r10 + kIsNil);           // lbz r9,N(r10)
  if (r9 == 0) {                                  // bne cr6,+0x4c (up)
    r11 = R32(base, r10);                         // lwz r11,0(r10)
    r9 = R8(base, r11 + kIsNil);                  // lbz r9,N(r11)
    while (r9 == 0) {                             // bne cr6,+0x44 (store r10)
      r10 = r11;                                  // mr r10,r11
      r11 = R32(base, r11);                       // lwz r11,0(r11)
      r9 = R8(base, r11 + kIsNil);                // lbz r9,N(r11); beq cr6,+0x30
    }
    store(r3, r10);                               // stw r10,0(r3)
    out.r9 = r9, out.r10 = r10, out.r11 = r11;
    return out;
  }
  r11 = R32(base, r11 + 4);                       // lwz r11,4(r11): the parent
  r10 = R8(base, r11 + kIsNil);                   // lbz r10,N(r11)
  if (r10 == 0) {                                 // bne cr6,+0x84 (store r11)
    for (;;) {
      r10 = at_r3;                                // lwz r10,0(r3)
      r9 = R32(base, r11 + 8);                    // lwz r9,8(r11)
      if (r10 != r9) break;                       // bne cr6,+0x84
      store(r3, r11);                             // stw r11,0(r3); rotlwi r11,r11,0
      at_r3 = r11;
      r11 = R32(base, r11 + 4);                   // lwz r11,4(r11)
      r10 = R8(base, r11 + kIsNil);               // lbz r10,N(r11)
      if (r10 != 0) break;                        // beq cr6,+0x5c (loop)
    }
  }
  store(r3, r11);                                 // stw r11,0(r3)
  out.r9 = r9, out.r10 = r10, out.r11 = r11;
  return out;
}

template <typename Stores>
Registers TreeNext17(const PPCContext& ctx, const uint8_t* base, Stores&& store) {
  return TreeNext<17>(ctx, base, std::forward<Stores>(store));
}
template <typename Stores>
Registers TreeNext21(const PPCContext& ctx, const uint8_t* base, Stores&& store) {
  return TreeNext<21>(ctx, base, std::forward<Stores>(store));
}

}  // namespace torchlight::hooks::guest_tree
