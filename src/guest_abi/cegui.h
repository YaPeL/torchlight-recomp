// The guest's CEGUI 0.6 (Runic's build) as System::renderGUI draws it: the System's redraw flag,
// the window tree walk of Window::render, the windows' rect caches and render caches, and the OGRE
// renderer's quad list. Used by the GUI redraw filter (hooks/gui_redraw_filter.h,
// --native_gui_redraw_filter). Reference: CEGUI 0.6.2's CEGUISystem.cpp, CEGUIWindow.cpp/.h and
// the OGRE 1.6 OgreCEGUIRenderer.cpp/.h; every offset below is confirmed by the recompiled code
// (the instruction and its address cited), and what differs from the reference is marked.

#pragma once

#include <cstdint>

#include "guest_abi/guest_functions.h"
#include "guest_abi/ogre_layout.h"

namespace torchlight::guest_abi::cegui {

using functions::GuestFunction;

// ---------------------------------------------------------------------------------------------
// CEGUI::System.
// [confirmed] The singleton pointer: Window::render loads it to reach the renderer's z
// (`lwz r11,26572(r6)` with r6 = 0x83500000, @0x821AFED0).
inline constexpr uint32_t kSystemGlobal = 0x835067CC;
// [confirmed] System::renderGUI (sub_821FBC60), as CEGUI 0.6.2's: when the redraw flag is set
// (`lbz r11,40(r3)`, @0x821FBC70) it resets the renderer's z (`stfs f0,24(r10)`, @0x821FBC8C),
// enables queueing (renderer slot 9, `lwz r7,36(r8)`), clears the quad list (slot 8,
// `lwz r5,32(r6)`), renders the active sheet (`lwz r3,48(r31)` then `bl 0x821afea0`, @0x821FBCC0)
// and clears the flag (`stb r11,40(r31)`, @0x821FBCD4). Then always: doRender (slot 7,
// `lwz r10,28(r11)`), queueing off, the mouse cursor (sub_82201050) and a Runic addition
// (sub_821FE020).
inline constexpr GuestFunction kSystemRenderGui{0x821FBC60, Confidence::kConfirmed};
inline constexpr Field kSystemRenderer{28, Confidence::kConfirmed};  // `lwz r10,28(r3)`
inline constexpr Field kSystemGuiRedraw{40, Confidence::kConfirmed};
inline constexpr Field kSystemActiveSheet{48, Confidence::kConfirmed};

// ---------------------------------------------------------------------------------------------
// CEGUI::Window.
// [confirmed] Window::render (sub_821AFEA0), as CEGUI 0.6.2's: returns unless isVisible(false)
// (`bl 0x821afe28` with r4 = 0); calls onRenderingStarted (vtable slot 27, `lwz r7,108(r10)`),
// drawSelf with the renderer's z (slot 58, `lwz r4,232(r5)`, f1 = `lfs f1,24(r30)`), steps the z
// down (`stfs f12,24(r30)`, @0x821AFF38), renders the children of the draw list in order and calls
// onRenderingEnded (slot 28, `lwz r10,112(r11)`). The loop's count is the child list's size
// (`lwz r10,48(r31)`, `lwz r9,44(r31)`, `srawi. r30,r8,2`) and its elements the draw list's
// (`lwz r11,60(r31)`, `lwzx r3,r11,r29`): both vectors hold the same windows.
inline constexpr GuestFunction kWindowRender{0x821AFEA0, Confidence::kConfirmed};
inline constexpr Field kWindowChildrenBegin{44, Confidence::kConfirmed};
inline constexpr Field kWindowChildrenEnd{48, Confidence::kConfirmed};
inline constexpr Field kWindowDrawListBegin{60, Confidence::kConfirmed};
// [confirmed] Window::isVisible(local_only) (sub_821AFE28): the window's flag (`lbz r11,353(r31)`,
// @0x821AFE64) and, unless local_only, the parent's isVisible (`lwz r3,80(r3)`, @0x821AFE38).
// Also guest_abi game_ui kWindowVisible and kWindowParent.
inline constexpr Field kWindowParent{80, Confidence::kConfirmed};
inline constexpr Field kWindowVisible{353, Confidence::kConfirmed};
// [confirmed] The slots Window::render calls, and CEGUI::Window's own functions in them (the
// Window vtable 0x8210119C): onRenderingStarted 0x821B00A8 and onRenderingEnded 0x821B00D0 only
// fire the event (EventSet at +20, its slot 5, with the names at 0x834B2250 and 0x834B22E8);
// drawSelf 0x821AFF88. The containers' drawSelf (TabControl, ClippedContainer,
// ScrolledContainer) is the empty 0x825BC790 (`blr`).
inline constexpr uint32_t kVtOnRenderingStarted = 108;
inline constexpr uint32_t kVtOnRenderingEnded = 112;
inline constexpr uint32_t kVtDrawSelf = 232;
inline constexpr uint32_t kOnRenderingStarted = 0x821B00A8;
inline constexpr uint32_t kOnRenderingEnded = 0x821B00D0;
inline constexpr uint32_t kDrawSelf = 0x821AFF88;
inline constexpr uint32_t kDrawSelfEmpty = 0x825BC790;
// [confirmed] Window::drawSelf (sub_821AFF88), as CEGUI 0.6.2's: when the window's d_needsRedraw
// is set (`lbz r11,424(r3)`) it clears the render cache (+392, `bl 0x825d6b50`), fills it through
// the window renderer (+580, its slot 1) or populateRenderCache (slot 59) and clears the flag
// (`stb r11,424(r31)`, @0x821AFFEC). Then, if the cache holds images (vector +392..+396) or text
// (+408..+412, `lwz r11,16(r30)` / `lwz r10,20(r30)` with r30 = +392), it renders it
// (RenderCache::render, sub_821B01B8) at the unclipped rect's position, clipped by the clipped
// rect, unless that has no width.
inline constexpr Field kWindowNeedsRedraw{424, Confidence::kConfirmed};
inline constexpr Field kWindowCacheImagesBegin{392, Confidence::kConfirmed};
inline constexpr Field kWindowCacheImagesEnd{396, Confidence::kConfirmed};
inline constexpr Field kWindowCacheTextsBegin{408, Confidence::kConfirmed};
inline constexpr Field kWindowCacheTextsEnd{412, Confidence::kConfirmed};
// [confirmed] The rect caches drawSelf reads (CEGUI Rect, 16 bytes: guest_abi game_ui
// cegui_rect). getUnclippedPixelRect (sub_821B00F8) returns +608..+623 when its flag is set
// (`lbz r11,624(r4)`) and otherwise computes and stores them and sets it (`stb r9,624(r30)`).
// getPixelRect (sub_821AFB68, the rect clipped by the parents) does the same with +648..+663 and
// +664. Window::notifyScreenAreaChanged (sub_825B1AF8), which onMoved and onSized reach, clears
// these and the inner rects' flags (`stb r29,624(r3)`, `stb r29,644(r3)`, `stb r29,664(r3)`,
// `stb r29,684(r3)`) and does the same for every child. (getUnclippedInnerPixelRect, sub_821FD7B8,
// is the one at +628 with flag +644.)
inline constexpr Field kWindowUnclippedRect{608, Confidence::kConfirmed};
inline constexpr Field kWindowUnclippedRectValid{624, Confidence::kConfirmed};
inline constexpr Field kWindowClippedRect{648, Confidence::kConfirmed};
inline constexpr Field kWindowClippedRectValid{664, Confidence::kConfirmed};
inline constexpr uint32_t kRectBytes = 16;

// ---------------------------------------------------------------------------------------------
// The OGRE renderer (OgreCEGUIRenderer, OGRE 1.6's, Runic's build).
// [confirmed] The current z (CEGUI::Renderer's d_current_z): renderGUI resets it (`stfs
// f0,24(r10)`) and Window::render reads and steps it (`lfs f1,24(r30)`).
inline constexpr Field kRendererZ{24, Confidence::kConfirmed};
// [confirmed] addQuad (sub_821D3638) reads the display area (d_display_area, a CEGUI Rect:
// `lfs f4,184(r30)` .. `lfs f11,196(r30)`) and the texel offset (d_texelOffset: `lfs f9,540(r30)`,
// `lfs f8,544(r30)`) to place every quad; marks the list unsorted (d_sorted, `stb r27,536(r30)`)
// and inserts the quad into d_quadlist (`addi r30,r30,200` then the insert). doRender
// (sub_821D2F80) reads the list's size (`lwz r11,208(r31)`) and, only when d_sorted is clear,
// refills its vertex buffers and sets it (`stb r9,536(r31)`): an unchanged list reuses them.
inline constexpr Field kRendererDisplayArea{184, Confidence::kConfirmed};
inline constexpr Field kRendererQuadList{200, Confidence::kConfirmed};
inline constexpr Field kRendererQuadListHead{204, Confidence::kConfirmed};
inline constexpr Field kRendererQuadListSize{208, Confidence::kConfirmed};
inline constexpr Field kRendererSorted{536, Confidence::kConfirmed};
// [confirmed] d_queueing: setQueueingEnabled (renderer slot 9, sub_821FBD30) is `stb r4,216(r3)`;
// renderGUI sets it before the rebuild and clears it after doRender.
inline constexpr Field kRendererQueueing{216, Confidence::kConfirmed};
// [runic_difference] clearRenderList (renderer slot 8, sub_821FDFB0) sets d_sorted (`stb
// r11,536(r31)`), erases the nodes (sub_821BA388), resets the head (`stw r8,4(r8)`, `stw r7,0(r7)`,
// `stw r6,8(r6)`) and the size (`stw r9,208(r31)`), and flips the index of the vertex buffer pair
// doRender fills (`stw r4,332(r31)`, (index - 1) & 1): a Runic addition, so a rebuilt list goes to
// the other buffer of the two.
inline constexpr Field kRendererBufferIndex{332, Confidence::kRunicDifference};
inline constexpr Field kRendererTexelOffset{540, Confidence::kConfirmed};
inline constexpr uint32_t kRendererTexelOffsetBytes = 8;
// [runic_difference] QuadInfo, 76 bytes (built on the stack at r1+128 by addQuad): the texture's
// Ogre::SharedPtr (16 bytes: its vtable `stw r7,128(r1)`, pRep, pUseCount, useFreeMethod), the
// position Rect (+16, `stfs f13,144(r1)`..), z (+32, `stfs f9,160(r1)`), a bool the reference does
// not have (+36, `stb r27,164(r1)`, set from the CEGUI texture's +29), the texture Rect (+40), the
// four colours (+56..+71) and the split mode (+72). The three bytes after the bool are padding,
// left as the node's memory had them.
inline constexpr uint32_t kQuadBytes = 76;
inline constexpr uint32_t kQuadPaddingBegin = 37;
inline constexpr uint32_t kQuadPaddingEnd = 40;
// [confirmed] d_quadlist is a std::multiset<QuadInfo> (MSVC's tree): the head node at
// kRendererQuadListHead (its parent the root, its left the first node), the size at
// kRendererQuadListSize; a node holds left (+0), parent (+4), right (+8), the QuadInfo (+12) and
// its nil flag (+89).
namespace quad_node {
inline constexpr Field kLeft{0, Confidence::kConfirmed};
inline constexpr Field kParent{4, Confidence::kConfirmed};
inline constexpr Field kRight{8, Confidence::kConfirmed};
inline constexpr Field kValue{12, Confidence::kConfirmed};
inline constexpr Field kIsNil{89, Confidence::kConfirmed};
// The node's allocation (`li r4,92` before `bl 0x821cd7f8`, sub_821D3990, which copies the
// QuadInfo field by field: the padding keeps what the memory had).
inline constexpr uint32_t kBytes = 92;
}  // namespace quad_node

// [confirmed] OgreCEGUIRenderer::doRender (renderer slot 7, reached from renderGUI's
// `lwz r10,28(r11)`).
inline constexpr GuestFunction kRendererDoRender{0x821D2F80, Confidence::kConfirmed};

}  // namespace torchlight::guest_abi::cegui
