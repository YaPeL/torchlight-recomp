#include "save_import/stash.h"

#include <map>

namespace torchlight::save_import {

std::vector<std::string> StashSlotProblems(const Parsed& stash) {
  std::map<int32_t, std::vector<size_t>> by_slot;
  const Node* items = stash.tree.Find("items");
  size_t index = 0;
  for (const Node& item : items ? items->items : std::list<Node>{}) {
    const Node* guid = item.Find("unit_guid");
    const Node* slot_node = item.Find("w2");
    const size_t current = index++;
    if (!guid || !slot_node) continue;
    const int64_t unit = Signed64(guid->number);
    if (unit == -1 || unit == 0) continue;  // the game discards it on load, on PC as on the 360
    const int32_t slot = static_cast<int32_t>(static_cast<uint32_t>(slot_node->number));
    if (slot == -1 || slot == 999) continue;  // the first free slot
    by_slot[slot].push_back(current);
  }
  std::vector<std::string> problems;
  for (const auto& [slot, list] : by_slot) {
    if (list.size() < 2) continue;
    std::string which;
    for (size_t i : list) which += (which.empty() ? "#" : ", #") + std::to_string(i);
    problems.push_back("slot " + std::to_string(slot) + " holds " + std::to_string(list.size()) +
                       " items (" + which + ")");
  }
  return problems;
}

int StashItemCount(const Schema& schema, std::span<const uint8_t> data) {
  SaveError error;
  auto parsed = Read360Stash(schema, data, error);
  if (!parsed) return -1;
  const Node* items = parsed->tree.Find("items");
  return items ? static_cast<int>(items->items.size()) : 0;
}

}  // namespace torchlight::save_import
