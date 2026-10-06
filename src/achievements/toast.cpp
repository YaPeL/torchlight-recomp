// The achievement unlock toast inside the game's own UI (CEGUI), in the style of the game's
// autosave notice: a framed, dimmed panel with a header plate and a title over the achievement's
// text.
// Works the same with any renderer, since the game draws it.
//
// The windows are created by the host (WindowManager::createWindow + setProperty, game_ui.h), not
// loaded from a layout, so no resource location is needed. They reference only the game's widget
// types, images and fonts by name; the texts are ours (display_names.h,
// data/ui/tl_achievement_strings.txt). Everything runs in the CGameUI::Update hook, on the game's
// thread (CEGUI is not thread-safe); unlocks arrive through the runtime's notification queue.
#include <chrono>
#include <filesystem>
#include <string>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include "achievements/display_names.h"
#include "achievements/runtime.h"
#include "achievements/toast_schedule.h"
#include "achievements/ui_strings.h"
#include "dev/guest_command.h"
#include "game_menu/guest_call.h"
#include "guest_abi/game_ui.h"
#include "guest_abi/game_ui_sheet.h"
#include "live/install.h"
#include "platform/platform.h"
#include "settings/host_settings.h"

namespace pc = torchlight::achievements;
namespace ui = torchlight::guest_abi::game_ui;
namespace sheet_abi = torchlight::guest_abi::game_ui_sheet;
using torchlight::game_menu::GuestCall;

namespace {

// Game thread only.
struct Toast {
  bool failed = false;
  uint32_t game_ui = 0;  // the CGameUI the windows were built for
  uint32_t root = 0, title = 0, text = 0;
  pc::ToastSchedule schedule;
};
Toast g_toast;

std::string Tr(std::string_view english) { return pc::Translate(english); }

void SetProperty(GuestCall& call, uint32_t window, const char* name, const std::string& value) {
  const uint32_t mark = call.Mark();
  const uint32_t n = call.CeguiString(name);
  const uint32_t v = call.CeguiString(value);
  if (n && v) call.Call(ui::kWindowSetProperty.address, {window, n, v});
  call.DestroyCeguiString(v);
  call.DestroyCeguiString(n);
  call.Release(mark);
}

// A new window of the game's `type`, named `name` (every window is named: the guest's generator of
// unique names clashes, game_ui.h kCreateWindow). 0 on failure.
uint32_t NewWindow(GuestCall& call, const char* type, const char* name,
                      std::initializer_list<std::pair<const char*, const char*>> properties) {
  const uint32_t window_manager = call.ReadU32(ui::kWindowManagerGlobal);
  if (!window_manager) return 0;
  const uint32_t mark = call.Mark();
  const uint32_t t = call.CeguiString(type);
  const uint32_t n = call.CeguiString(name);
  const uint32_t prefix = call.CeguiString("");
  const uint32_t window =
      t && n && prefix ? call.Call(ui::kCreateWindow.address, {window_manager, t, n, prefix}) : 0;
  call.DestroyCeguiString(prefix);
  call.DestroyCeguiString(n);
  call.DestroyCeguiString(t);
  call.Release(mark);
  if (!window) return 0;
  // Never in the way: no input, no navigation (no tabOrder), drawn over its siblings.
  SetProperty(call, window, "MousePassThroughEnabled", "True");
  SetProperty(call, window, "ClippedByParent", "False");
  for (const auto& [property, value] : properties) SetProperty(call, window, property, value);
  return window;
}


// Builds the toast, hidden, on the game UI's sheet. The names are fixed, so this runs once per
// process: CGameUI is built once (game_ui.h kGameUiCreateMenus).
bool Build(GuestCall& call, uint32_t game_ui, uint32_t sheet) {
  // Sizes in the game's 1280x720 layout units, as its own layouts; kGameUiScaleLayout adapts them.
  const uint32_t root = NewWindow(call, "ceguiWidget/ItemText", "tl_achievement_toast",
      {{"UnifiedAreaRect", "{{0,0},{0,48},{0,440},{0,128}}"},
       {"HorizontalAlignment", "Centre"},
       {"AlwaysOnTop", "True"},
       {"Visible", "False"}});
  // The game's own dimming panel (as in messagehousing_xenon.uilayout): black at 0.7 alpha over
  // the whole frame, added first so the header and the text draw over it.
  const uint32_t backdrop = root ? NewWindow(call, "ceguiWidget/StaticImage",
      "tl_achievement_toast/backdrop",
      {{"Image", "set:widgets2 image:black"},
       {"Alpha", "0.7"},
       {"UnifiedAreaRect", "{{0,-1},{0,-1},{1,1},{1,1}}"}}) : 0;
  const uint32_t header = backdrop ? NewWindow(call, "ceguiWidget/StaticImage",
      "tl_achievement_toast/header",
      {{"Image", "set:widgets2 image:facet_panel_bottom"},
       {"UnifiedAreaRect", "{{0,0},{0,-19},{0,322},{0,17}}"},
       {"HorizontalAlignment", "Centre"},
       {"AlwaysOnTop", "True"}}) : 0;
  const uint32_t title = header ? NewWindow(call, "ceguiWidget/StaticTextOutline",
      "tl_achievement_toast/title",
      {{"Font", "SerifBig"},
       {"UnifiedAreaRect", "{{0,0},{0,4},{0,299},{0,30}}"},
       {"HorzTextFormatting", "CentreAligned"},
       {"HorizontalAlignment", "Centre"}}) : 0;
  const uint32_t text = title ? NewWindow(call, "ceguiWidget/StaticText",
      "tl_achievement_toast/text",
      {{"Font", "Serif14"},
       {"UnifiedAreaRect", "{{0,12},{0,22},{1,-12},{1,-8}}"},
       {"HorzTextFormatting", "WordWrapCentreAligned"},
       {"VertFormatting", "CentreAligned"}}) : 0;
  if (!text) return false;
  call.Call(ui::kAddChildWindow.address, {root, backdrop});
  call.Call(ui::kAddChildWindow.address, {header, title});
  call.Call(ui::kAddChildWindow.address, {root, header});
  call.Call(ui::kAddChildWindow.address, {root, text});
  // As the game prepares its own layouts: scaled for the screen's aspect (the texts are set later,
  // so its translator never sees them).
  call.Call(ui::kGameUiScaleLayout.address, {game_ui, root, 0});
  call.Call(ui::kAddChildWindow.address, {sheet, root});
  g_toast.game_ui = game_ui;
  g_toast.root = root;
  g_toast.title = title;
  g_toast.text = text;
  REXLOG_INFO("achievement toast: built 0x{:08X} on the game UI sheet 0x{:08X}", root, sheet);
  return true;
}

void Update(PPCContext& ctx, uint8_t* base, uint32_t game_ui) {
  if (g_toast.failed) return;
  if (g_toast.game_ui && g_toast.game_ui != game_ui) {
    REXLOG_WARN("achievement toast: game UI 0x{:08X} replaced by 0x{:08X}; toasts off",
                g_toast.game_ui, game_ui);
    g_toast.failed = true;
    return;
  }
  const uint32_t sheet = torchlight::guest_abi::ReadU32(base, game_ui, sheet_abi::game_ui_object::kSheet);
  const bool covered = !sheet || sheet_abi::Covered(base, game_ui);
  const double now = std::chrono::duration<double>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  const auto step = g_toast.schedule.Update(now, covered, [] { return pc::NextNotification(); });
  if (step.action == pc::ToastSchedule::Action::kNone) return;
  GuestCall call(ctx, base);
  if (step.action == pc::ToastSchedule::Action::kHide) {
    SetProperty(call, g_toast.root, "Visible", "False");
    return;
  }
  if (!g_toast.root && !Build(call, game_ui, sheet)) {
    REXLOG_ERROR("achievement toast: cannot create its windows; toasts off");
    g_toast.failed = true;
    return;
  }
  SetProperty(call, g_toast.title, "Text", Tr(pc::kUnlockedTitle));
  const std::string official = pc::NotificationText(step.id);  // Xbox set: already localised
  SetProperty(call, g_toast.text, "Text", official.empty() ? Tr(pc::EnglishName(step.id)) : official);
  SetProperty(call, g_toast.root, "Visible", "True");
  REXLOG_INFO("achievement toast: {}", step.id);
}

}  // namespace

#define FUNCTION_ADDRESS_CHECK(entry, addr) \
  static_assert(sheet_abi::entry.address == 0x##addr##u, "game_ui_sheet mismatch")

extern "C" {

FUNCTION_ADDRESS_CHECK(kGameUiUpdate, 821A40E0);
REX_EXTERN(__imp__sub_821A40E0);
REX_FUNC(sub_821A40E0) {
  const uint32_t game_ui = ctx.r3.u32;
  const uint32_t context = ctx.r5.u32;
  __imp__sub_821A40E0(ctx, base);
  if (game_ui && pc::NotificationsShown()) Update(ctx, base, game_ui);
  if (game_ui) torchlight::dev::OnGameUiUpdate(ctx, base, game_ui, context);
}

}  // extern "C"
