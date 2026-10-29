// Who asks CEGUI to redraw the whole GUI: the guest functions that set the System's redraw flag
// (game_ui kSystemGuiRedraw), for the diagnostic that counts them per frame
// (hooks/gui_redraw_census_hooks.cpp).

#pragma once

#include <cstdint>

namespace torchlight::guest_abi::gui_redraw {

// [confirmed] Every function of the generated code that loads the System singleton
// (`lwz rX,26572(rY)`, game_ui kCeguiSystemGlobal) and stores a byte at +40 (`stb rX,40(rY)`),
// found by scanning torchlight_recomp.*.cpp: 108 functions, among them Window::onSized
// 0x821A3ED0, Window::onMoved 0x821A4938, onTextChanged 0x825B06A0, onFontChanged 0x825B06E0,
// onAlphaChanged 0x825B0760, onShown 0x825B0860, onHidden 0x825B08A0 (requestRedraw is inlined in
// each). Functions that set the flag through a System pointer they did not load themselves are
// not in it: their requests show as unattributed.
#define TORCHLIGHT_GUI_REDRAW_WRITERS(X)                                                           \
  X(821A3ED0) X(821A4938) X(823392E8) X(825AFE50) X(825B06A0) X(825B06E0) X(825B0760) X(825B0860) \
  X(825B08A0) X(825B0928) X(825B0A00) X(825B0AD8) X(825B0B60) X(825B0BA0) X(825B0CB8) X(825B0D30) \
  X(825B1000) X(825B1040) X(825BBDB8) X(825BC188) X(825BD958) X(825BD9D0) X(825BDA10) X(825BDA78) \
  X(825BDAB8) X(825BE108) X(825CBA10) X(825CBA50) X(825CBA90) X(825CBB78) X(825CBBB8) X(825CBC48) \
  X(825CC860) X(825CC910) X(825CCF88) X(825CE608) X(825CEED0) X(825CF210) X(825D80B0) X(825DCA10) \
  X(825DCE08) X(825E3648) X(825EA5E8) X(825EA680) X(825EA768) X(825EA850) X(825EA8B8) X(825EA938) \
  X(825EC578) X(825ED140) X(825ED230) X(825ED720) X(825EDD00) X(825F0780) X(825F08C8) X(825F0960) \
  X(825F0AE8) X(825F0DD8) X(825F0F18) X(825F0F98) X(825F0FF0) X(825F1DB0) X(825F3008) X(825F4490) \
  X(825F4520) X(825F6F68) X(825F6FE0) X(825F7020) X(825F7088) X(825F70C8) X(825F7108) X(825F7148) \
  X(825F7FC8) X(825FA068) X(825FA0A8) X(825FA120) X(825FA160) X(825FA1A0) X(825FA218) X(825FA720) \
  X(825FE5E0) X(825FE9F0) X(825FEA88) X(825FEAF8) X(825FF630) X(825FF750) X(826000B0) X(82600280) \
  X(82600338) X(82600378) X(82600640) X(82600BD0) X(826018F8) X(82602208) X(82602400) X(82606598) \
  X(82607108) X(82608F88) X(82609E78) X(8260A1C0) X(8265BF50) X(8265C070) X(8265FC28) X(8265FCF0) \
  X(8265FDA8) X(8265FE90) X(8265FFB8) X(82660168)

// [confirmed] CEGUI::System::renderGUI: reads the flag (+40) at its start to decide whether to
// re-queue the tree (sub_821FBC60); the census's frame boundary.
inline constexpr uint32_t kRenderGui = 0x821FBC60;

}  // namespace torchlight::guest_abi::gui_redraw
