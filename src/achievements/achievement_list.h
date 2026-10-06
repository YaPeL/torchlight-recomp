// The list behind the game's "Achievements" button (XamShowAchievementsUI is an SDK stub): an ImGui
// modal of the set in use, drawn by the runtime's ImGui drawer (only mode: live's UiOverlay over the
// native backend; Xenos: the SDK's). Create on the UI thread; the dialog deletes itself when closed.
#pragma once

namespace rex {
class Runtime;
namespace ui {
class ImmediateDrawer;
}
}  // namespace rex

namespace torchlight::achievements {
// Opens the list of the set in use, unless one is already open. `immediate_drawer` draws the Xbox
// icons (null: no icons).
void OpenAchievementList(rex::Runtime* runtime, rex::ui::ImmediateDrawer* immediate_drawer);
}  // namespace torchlight::achievements
