// What the PC achievement list shows for each of the 66 IDs: unlocked or not, counter progress, and
// whether it can be earned in this version (docs/achievement-coverage.md). No UI here: the ImGui list
// (achievement_list.cpp) draws these rows.
#pragma once
#include <string_view>
#include <vector>
#include "achievements/service.h"
namespace torchlight::achievements {
enum class Availability {
  kAvailable,
  kOutOfScope,   // mods are not restored (MODS_1/5/10)
};
struct ListRow {
  std::string_view id;
  std::string_view english;  // display_names.h
  bool unlocked = false;
  bool counter = false;      // a stat threshold (progress = value / threshold)
  int32_t value = 0, threshold = 0;
  Availability availability = Availability::kAvailable;
};
Availability AvailabilityOf(std::string_view id);
// Catalog order.
std::vector<ListRow> BuildList(const State& state);
} // namespace torchlight::achievements
