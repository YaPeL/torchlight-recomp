#include "game_menu/guest_message_box.h"

#include <cstring>

#include <rex/logging.h>
#include <rex/ppc/func.h>
#include <rex/runtime.h>
#include <rex/system/xmemory.h>
#include <rex/system/xtypes.h>

#include "guest_abi/ogre_layout.h"
#include "guest_abi/save_menu.h"
#include "save_import/pak.h"

// The runtime's XamShowMessageBoxUI export (the import thunk's target), called directly: GuestCall
// passes eight arguments, and this one takes nine.
extern "C" REX_FUNC(__imp__XamShowMessageBoxUI);

namespace torchlight::game_menu {

namespace {

namespace abi = torchlight::guest_abi;
using rex::X_RESULT;  // the X_ERROR_* macros expand to it

// UTF-16BE, zero terminated, at `address`.
uint32_t PutText(uint8_t* base, uint32_t address, const std::string& utf8) {
  const std::u16string text = save_import::Utf16(utf8);
  for (char16_t unit : text) {
    uint8_t* bytes = abi::xbox_memory::HostAddress(base, address);
    bytes[0] = static_cast<uint8_t>(unit >> 8);
    bytes[1] = static_cast<uint8_t>(unit);
    address += 2;
  }
  uint8_t* end = abi::xbox_memory::HostAddress(base, address);
  end[0] = end[1] = 0;
  return address + 2;
}

size_t TextBytes(const std::string& utf8) { return (save_import::Utf16(utf8).size() + 1) * 2; }

}  // namespace

void GuestMessageBox::Free() {
  if (block_ && runtime_) runtime_->memory()->SystemHeapFree(block_);
  block_ = result_ = overlapped_ = 0;
}

bool GuestMessageBox::Show(PPCContext& ctx, uint8_t* base, const std::string& title,
                           const std::string& text, const std::vector<std::string>& buttons,
                           int active) {
  // A block still in use would be freed under the runtime.
  if (block_ || !runtime_) return false;
  // result (4) | XOVERLAPPED (28) | button pointers | texts
  const uint32_t fixed = 4 + 28 + 4 * static_cast<uint32_t>(buttons.size());
  size_t size = fixed + TextBytes(title) + TextBytes(text);
  for (const auto& button : buttons) size += TextBytes(button);
  const uint32_t block = runtime_->memory()->SystemHeapAlloc(static_cast<uint32_t>(size + 16));
  if (!block) return false;
  std::memset(abi::xbox_memory::HostAddress(base, block), 0, size + 16);
  block_ = block;
  result_ = block;
  overlapped_ = block + 4;
  abi::WriteU32(base, overlapped_, X_ERROR_IO_PENDING);  // XOVERLAPPED.result
  const uint32_t button_list = block + 32;
  const uint32_t title_at = button_list + 4 * static_cast<uint32_t>(buttons.size());
  const uint32_t text_at = PutText(base, title_at, title);
  uint32_t next = PutText(base, text_at, text);
  for (size_t i = 0; i < buttons.size(); ++i) {
    abi::WriteU32(base, button_list + 4 * static_cast<uint32_t>(i), next);
    next = PutText(base, next, buttons[i]);
  }

  // Nine arguments: the ninth on the stack, below the caller's (the hooked function has returned,
  // nothing there is live).
  PPCContext call = ctx;
  const uint32_t stack = (ctx.r1.u32 - 0x200) & ~0xFu;
  call.r1.u64 = stack;
  call.r3.u64 = 0;  // user index
  call.r4.u64 = title_at;
  call.r5.u64 = text_at;
  call.r6.u64 = buttons.size();
  call.r7.u64 = button_list;
  call.r8.u64 = static_cast<uint32_t>(active);
  call.r9.u64 = 0;  // flags: no icon
  call.r10.u64 = result_;
  abi::WriteU32(base, stack + abi::save_menu::kStackArgumentOffset, overlapped_);
  __imp__XamShowMessageBoxUI(call, base);
  if (call.r3.u32 != X_ERROR_IO_PENDING && call.r3.u32 != X_ERROR_SUCCESS) {
    REXLOG_ERROR("message box: XamShowMessageBoxUI did not open it ({:08X})", call.r3.u32);
    Free();
    return false;
  }
  return true;
}

std::optional<int> GuestMessageBox::Poll(uint8_t* base) {
  if (!block_) return -1;
  const uint32_t status = abi::ReadU32(base, overlapped_);
  if (status == X_ERROR_IO_PENDING) return std::nullopt;
  const int button = status == X_ERROR_SUCCESS ? static_cast<int>(abi::ReadU32(base, result_)) : -1;
  Free();
  return button;
}

}  // namespace torchlight::game_menu
