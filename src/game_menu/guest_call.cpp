#include "game_menu/guest_call.h"

#include <bit>
#include <cstring>

#include <rex/logging.h>
#include <rex/ppc/func.h>

#include "guest_abi/game_ui.h"
#include "guest_abi/ogre_layout.h"

namespace torchlight::game_menu {

namespace abi = torchlight::guest_abi;
namespace ui = torchlight::guest_abi::game_ui;

namespace {
// Room the callees may use above their stack pointer is none (they save below it); a margin
// between the scratch area and the callees' stack anyway.
constexpr uint32_t kMargin = 0x100;
}  // namespace

GuestCall::GuestCall(PPCContext& ctx, uint8_t* base, uint32_t scratch_bytes)
    : ctx_(ctx), saved_(ctx), base_(base) {
  const uint32_t caller_sp = ctx.r1.u32;
  scratch_end_ = (caller_sp - kMargin) & ~0xFu;
  scratch_ = (scratch_end_ - scratch_bytes) & ~0xFu;
  stack_top_ = (scratch_ - kMargin) & ~0xFu;
}

GuestCall::~GuestCall() { ctx_ = saved_; }

uint32_t GuestCall::Reserve(uint32_t size) {
  const uint32_t aligned = (size + 0xF) & ~0xFu;
  if (scratch_ + aligned > scratch_end_) {
    REXLOG_ERROR("game menu: guest call scratch area full");
    return 0;
  }
  const uint32_t at = scratch_;
  std::memset(abi::xbox_memory::HostAddress(base_, at), 0, aligned);
  scratch_ += aligned;
  return at;
}

uint32_t GuestCall::PushUtf8(std::string_view text) {
  const uint32_t at = Reserve(uint32_t(text.size()) + 1);
  if (at) std::memcpy(abi::xbox_memory::HostAddress(base_, at), text.data(), text.size());
  return at;
}

uint32_t GuestCall::Call(uint32_t address, std::initializer_list<uint32_t> args) {
  PPCFunc* function = rex::runtime::ResolveIndirectFunction(address);
  if (!function) {
    REXLOG_ERROR("game menu: no guest function at 0x{:08X}", address);
    return 0;
  }
  PPCRegister* const registers[] = {&ctx_.r3, &ctx_.r4, &ctx_.r5, &ctx_.r6,
                                    &ctx_.r7, &ctx_.r8, &ctx_.r9, &ctx_.r10};
  size_t i = 0;
  for (uint32_t arg : args) {
    if (i == std::size(registers)) break;
    registers[i++]->u64 = arg;
  }
  ctx_.r1.u64 = stack_top_;
  ctx_.lr = 0;
  function(ctx_, base_);
  return ctx_.r3.u32;
}

uint32_t GuestCall::CallVirtual(uint32_t object, uint32_t slot,
                                std::initializer_list<uint32_t> args) {
  const uint32_t vtable = ReadU32(object);
  const uint32_t target = ReadU32(vtable + slot * 4);
  uint32_t all[8] = {object};
  size_t n = 1;
  for (uint32_t arg : args) {
    if (n == std::size(all)) break;
    all[n++] = arg;
  }
  switch (n) {  // initializer_list has no runtime constructor
    case 1: return Call(target, {all[0]});
    case 2: return Call(target, {all[0], all[1]});
    case 3: return Call(target, {all[0], all[1], all[2]});
    case 4: return Call(target, {all[0], all[1], all[2], all[3]});
    default: return Call(target, {all[0], all[1], all[2], all[3], all[4]});
  }
}

uint32_t GuestCall::ReadU32(uint32_t address) const { return abi::ReadU32(base_, address); }

void GuestCall::WriteU32(uint32_t address, uint32_t value) {
  const uint32_t big = std::byteswap(value);
  std::memcpy(abi::xbox_memory::HostAddress(base_, address), &big, 4);
}

uint32_t GuestCall::CeguiString(std::string_view utf8) {
  const uint32_t text = PushUtf8(utf8);
  const uint32_t str = Reserve(ui::kCeguiStringSize);
  if (!text || !str) return 0;
  Call(ui::kCeguiStringFromUtf8.address, {str, text});
  return str;
}

void GuestCall::DestroyCeguiString(uint32_t str) {
  if (str) Call(ui::kCeguiStringDtor.address, {str});
}

uint32_t GuestCall::StdString(std::string_view text) {
  const uint32_t data = PushUtf8(text);
  const uint32_t str = Reserve(abi::ogre::stl_string::kSize.bytes);
  if (!data || !str) return 0;
  // Empty string (buffer[0] = 0, length 0, capacity 15), then assign: as the resources.cfg loader
  // builds its strings.
  WriteU32(str + abi::ogre::stl_string::kCapacity.offset, abi::ogre::stl_string::kInlineCapacity);
  // assign allocates the text on the guest heap past the inline capacity.
  Call(ui::kStringAssign.address, {str, data, uint32_t(text.size())});
  return str;
}

void GuestCall::DestroyStdString(uint32_t str) {
  if (!str) return;
  // As the game frees its strings inline (game_ui.h kStringAssign).
  if (ReadU32(str + abi::ogre::stl_string::kCapacity.offset) > abi::ogre::stl_string::kInlineCapacity) {
    Call(ui::kFree.address, {ReadU32(str + abi::ogre::stl_string::kBuffer.offset)});
  }
}

}  // namespace torchlight::game_menu
