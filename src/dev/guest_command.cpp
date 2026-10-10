#include "dev/guest_command.h"

#include <atomic>
#include <deque>
#include <mutex>
#include <string>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "dev/command_list.h"

#ifdef TORCHLIGHT_DEV_COMMANDS
#include "game_menu/guest_call.h"
#include "guest_abi/achievements.h"
#include "guest_abi/dev_console.h"
#include "guest_abi/game_ui.h"
#include "guest_abi/game_ui_sheet.h"
#include "guest_abi/xbox_memory.h"
#endif

REXCVAR_DEFINE_STRING(dev_guest_command, "", "Torchlight",
                      "Development builds only: guest developer commands; ';' separates the steps, "
                      "one after each level load, '&' the commands of a step, run together "
                      "(e.g. \"FAME 1000;DUNGEON X & DESCEND\")");

namespace torchlight::dev {
namespace {
std::once_flag g_parsed;
std::deque<std::vector<std::string>> g_pending;  // steps; game thread after parsing
std::atomic<bool> g_armed{false};
std::mutex g_queued_mutex;
std::deque<std::string> g_queued;  // QueueGuestCommand

void Parse() {
  const std::string value = REXCVAR_GET(dev_guest_command);
  if (value.empty()) return;
#ifndef TORCHLIGHT_DEV_COMMANDS
  REXLOG_WARN("dev: --dev_guest_command ignored: this build has no developer commands "
              "(CMake option TORCHLIGHT_DEV_COMMANDS)");
#else
  for (auto& step : ParseCommandList(value)) g_pending.push_back(std::move(step));
  REXLOG_WARN("dev: {} guest developer command step(s) queued; one runs after each level load",
              g_pending.size());
#endif
}

#ifdef TORCHLIGHT_DEV_COMMANDS
// One command through the game's executor, passed by value as a guest std::wstring (UTF-16BE text,
// then the game's constructor and copy), like the HUD does.
void Run(PPCContext& ctx, uint8_t* base, uint32_t console, const std::string& command) {
  namespace console_abi = torchlight::guest_abi::dev_console;
  namespace ui = torchlight::guest_abi::game_ui;
  game_menu::GuestCall call(ctx, base);
  const uint32_t text = call.Reserve(uint32_t(2 * (command.size() + 1)));
  const uint32_t source = call.Reserve(0x1C);
  const uint32_t argument = call.Reserve(0x1C);
  if (!text || !source || !argument) {
    REXLOG_ERROR("dev: no scratch space for \"{}\"", command);
    return;
  }
  uint8_t* units = torchlight::guest_abi::xbox_memory::HostAddress(base, text);
  for (size_t i = 0; i < command.size(); ++i) {
    units[2 * i] = 0;
    units[2 * i + 1] = uint8_t(command[i]);
  }
  units[2 * command.size()] = units[2 * command.size() + 1] = 0;
  call.Call(ui::kWStringFromText.address, {source, text});
  call.Call(console_abi::kWStringCopy.address, {argument, source});
  const uint32_t result = call.Call(console_abi::kExecuteCommand.address, {console, argument});
  call.Call(ui::kWStringDtor.address, {source});  // the executor destroys its own argument
  REXLOG_WARN("dev: guest command \"{}\" -> {} ({} step(s) left)", command, result, g_pending.size());
}
#endif
}  // namespace

bool QueueGuestCommand(std::string command) {
#ifndef TORCHLIGHT_DEV_COMMANDS
  (void)command;
  return false;
#else
  std::lock_guard lock(g_queued_mutex);
  g_queued.push_back(std::move(command));
  return true;
#endif
}

void OnGameLevelLoaded() {
  std::call_once(g_parsed, Parse);
  g_armed = true;
}

void OnGameUiUpdate(PPCContext& ctx, uint8_t* base, uint32_t game_ui, uint32_t context) {
#ifndef TORCHLIGHT_DEV_COMMANDS
  (void)ctx; (void)base; (void)game_ui; (void)context;
#else
  bool queued = false;
  {
    std::lock_guard lock(g_queued_mutex);
    queued = !g_queued.empty();
  }
  if (!game_ui || ((!g_armed || g_pending.empty()) && !queued)) return;
  namespace sheet = torchlight::guest_abi::game_ui_sheet;
  namespace console_abi = torchlight::guest_abi::dev_console;
  // CGameUI::Update's own state test: context +5124 == 1 is the in-game HUD (game_ui_sheet.h).
  if (!context ||
      torchlight::guest_abi::ReadU32(base, context + torchlight::guest_abi::achievements::kContextEligibility) != 1 ||
      sheet::Covered(base, game_ui)) {
    return;
  }
  const uint32_t console = torchlight::guest_abi::ReadU32(base, game_ui + console_abi::kConsole.offset);
  if (!console) {
    REXLOG_ERROR("dev: no guest console object; commands dropped");
    g_pending.clear();
    return;
  }
  if (queued) {
    std::deque<std::string> commands;
    {
      std::lock_guard lock(g_queued_mutex);
      commands.swap(g_queued);
    }
    for (const auto& command : commands) Run(ctx, base, console, command);
    return;
  }
  g_armed = false;
  const std::vector<std::string> step = g_pending.front();
  g_pending.pop_front();
  for (const auto& command : step) Run(ctx, base, console, command);
#endif
}

}  // namespace torchlight::dev
