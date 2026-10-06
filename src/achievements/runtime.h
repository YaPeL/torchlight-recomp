#pragma once
#include "achievements/diagnostics.h"
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
namespace rex::system {
class AchievementManager;
}

namespace torchlight::achievements {
// Reads the mode cvars; the profile state lives in `state_dir` (the app passes the user data
// folder's achievements/, platform::DataDir()).
void Install(const std::filesystem::path& state_dir);
void Shutdown();
void PrepareForClose(); // flush before SDK window-close hard exit; no Steam calls
void Pump();
bool NativeEnabled();
// Whether the PC set has this achievement (false while disabled). Takes the state mutex.
bool Unlocked(std::string_view id);
bool DiagnosticsEnabled();
// Unlock notifications (notifications.h): whether this mode shows them, and the next newly
// unlocked ID to announce. Thread-safe; empty when notifications are off.
bool NotificationsShown();
std::optional<std::string> NextNotification();
// Text to show for a notification ID instead of our own (the Xbox set's official name); empty for
// the PC set.
std::string NotificationText(const std::string& id);

// The achievement set from the settings (settings.toml "achievements"), before Install: "pc" turns
// the native PC mode on (local) unless --pc_achievements already chose a mode; "xbox" leaves it off
// unless the command line asked for one.
void ApplyAchievementSet(bool pc);
// Xbox set: the game's own unlocks go through the SDK (achievements/58410A7E.toml); announce them
// with our toast, using the SDK's names (from the XEX, in the console language). Call once the
// kernel exists; the SDK's own toast is not created (TorchlightApp).
void UseXboxAchievements(rex::system::AchievementManager& manager);

// A copy of the PC profile's state (for the achievement list); empty when native mode is off.
std::optional<State> SnapshotState();
// The game's "Achievements" button (XamShowAchievementsUI): the app sets what opens the list of the
// set in use; ShowAchievementList runs it and returns false when none is set.
void SetAchievementListHandler(std::function<void()> handler);
bool ShowAchievementList();
void Apply(const std::function<void(Service&)>& action, const Observation& observation = {});
// One of PC's stats flush points: evaluates the counter changes since the last one (Service::Flush);
// new unlocks are persisted and announced like any other.
void FlushCounters(const Observation& observation = {});
} // namespace torchlight::achievements
