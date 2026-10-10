#include "game_menu/save_mod_list.h"

#include <span>
#include <string>
#include <vector>

#include "guest_abi/mods.h"

namespace torchlight::game_menu {

namespace {

namespace abi = torchlight::guest_abi;
namespace mods_abi = torchlight::guest_abi::mods;

uint64_t ReadS64Field(const uint8_t* base, uint32_t addr) {
  return (uint64_t{abi::ReadU32(base, addr)} << 32) | abi::ReadU32(base, addr + 4);
}

void WriteS64Field(uint8_t* base, uint32_t addr, uint64_t value) {
  abi::WriteU32(base, addr, static_cast<uint32_t>(value >> 32));
  abi::WriteU32(base, addr + 4, static_cast<uint32_t>(value));
}

}  // namespace

UnitSaveOutcome WriteUnitSave(uint8_t* base, uint32_t save_data, uint32_t stream,
                              const std::function<void()>& write_unit) {
  namespace unit = mods_abi::unit_save;
  namespace stream_abi = mods_abi::save_stream;
  const std::vector<std::u16string> names = mods_abi::ReadSavedModNames(base, save_data);
  if (names.empty()) {
    write_unit();
    return {};
  }
  const uint64_t start = ReadS64Field(base, stream + stream_abi::kPosition.offset);
  const uint64_t size = ReadS64Field(base, stream + stream_abi::kSize.offset);
  const uint32_t before = abi::ReadU32(base, save_data + unit::kBeforeModNames.offset);
  write_unit();

  // The buffer may have moved while growing: read it again.
  const uint64_t end = ReadS64Field(base, stream + stream_abi::kPosition.offset);
  const uint32_t buffer = abi::ReadU32(base, stream + stream_abi::kBuffer.offset);
  UnitSaveOutcome outcome;
  outcome.names = names.size();
  outcome.repair = mods::ModListRepair::kNotFound;
  if (buffer && end >= start && end - start <= (uint64_t{1} << 26)) {
    outcome.repair = mods::RepairSavedModList(
        std::span<uint8_t>(base + buffer + start, static_cast<size_t>(end - start)), before, names);
  }
  if (outcome.repair == mods::ModListRepair::kRepaired) {
    outcome.kind = UnitSaveOutcome::Kind::kRepaired;
    return outcome;
  }
  outcome.kind = UnitSaveOutcome::Kind::kWrittenWithoutList;
  WriteS64Field(base, stream + stream_abi::kPosition.offset, start);
  WriteS64Field(base, stream + stream_abi::kSize.offset, size);
  const uint32_t count = abi::ReadU32(base, save_data + unit::kModNamesCount.offset);
  abi::WriteU32(base, save_data + unit::kModNamesCount.offset, 0);
  write_unit();
  abi::WriteU32(base, save_data + unit::kModNamesCount.offset, count);
  return outcome;
}

}  // namespace torchlight::game_menu
