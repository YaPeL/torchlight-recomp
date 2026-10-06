// Calls into guest code from a hook, on the game's thread (CEGUI and the game's UI are not
// thread-safe: never from a host thread).
//
// Runs on the hook's own context (the thread's: runtime imports the callees reach may rely on it)
// and puts every register back when done, so the caller sees them as the hooked function returned
// them. The calls get a frame of their own below the caller's stack pointer: a scratch area for the
// arguments that live in memory (strings, out structures), and below it the stack the callees use.
// The hooked function has returned by then, so nothing below the caller's stack pointer is live.

#pragma once

#include <cstdint>
#include <initializer_list>
#include <string_view>

#include <rex/ppc/context.h>

namespace torchlight::game_menu {

class GuestCall {
 public:
  GuestCall(PPCContext& ctx, uint8_t* base, uint32_t scratch_bytes = 0x1000);
  ~GuestCall();
  GuestCall(const GuestCall&) = delete;
  GuestCall& operator=(const GuestCall&) = delete;

  // Guest addresses inside the scratch area (16-byte aligned, zeroed). Zero when it is full.
  uint32_t Reserve(uint32_t size);
  uint32_t PushUtf8(std::string_view text);  // NUL terminated
  // Scratch space taken after Mark() is given back by Release(mark) (for helpers that destroy
  // what they built before returning).
  uint32_t Mark() const { return scratch_; }
  void Release(uint32_t mark) { scratch_ = mark; }

  // Calls the guest function at `address` with up to 8 integer arguments (r3..r10); returns r3.
  uint32_t Call(uint32_t address, std::initializer_list<uint32_t> args);
  // Calls `object`'s vtable slot `slot` with `object` as r3 and `args` after it.
  uint32_t CallVirtual(uint32_t object, uint32_t slot, std::initializer_list<uint32_t> args);

  uint32_t ReadU32(uint32_t address) const;
  void WriteU32(uint32_t address, uint32_t value);
  void WriteU8(uint32_t address, uint8_t value) { base_[address] = value; }
  uint8_t* base() const { return base_; }

  // Guest strings built in the scratch area (empty guest address on failure); both may allocate
  // their text on the guest heap, so destroy them when done.
  uint32_t CeguiString(std::string_view utf8);
  void DestroyCeguiString(uint32_t str);
  uint32_t StdString(std::string_view text);
  void DestroyStdString(uint32_t str);

 private:
  PPCContext& ctx_;
  PPCContext saved_;
  uint8_t* base_;
  uint32_t stack_top_ = 0;  // the callees' stack pointer
  uint32_t scratch_ = 0, scratch_end_ = 0;
};

}  // namespace torchlight::game_menu
