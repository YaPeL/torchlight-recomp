#include "achievements/list_model.h"
#include <algorithm>
#include "achievements/display_names.h"
namespace torchlight::achievements {
Availability AvailabilityOf(std::string_view id) {
  if (id == "MODS_1" || id == "MODS_5" || id == "MODS_10") return Availability::kOutOfScope;
  return Availability::kAvailable;
}
std::vector<ListRow> BuildList(const State& state) {
  std::vector<ListRow> rows;
  rows.reserve(kCatalog.size());
  for (const auto& d : kCatalog) {
    ListRow row;
    row.id = d.id;
    row.english = EnglishName(d.id);
    row.unlocked = state.unlocked.contains(std::string(d.id));
    row.counter = d.stat < state.stats.size();  // selector 30 = explicit completion
    if (row.counter) {
      row.threshold = d.threshold;
      row.value = std::clamp(state.stats[d.stat], 0, d.threshold);
    }
    row.availability = AvailabilityOf(d.id);
    rows.push_back(row);
  }
  return rows;
}
} // namespace torchlight::achievements
