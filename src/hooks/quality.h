// Graphics quality levels for slow devices, made of the game's own settings (Runic's CSettings,
// guest_abi/game_settings.h) as the PC version's detail levels make them (OpenTorchlight,
// decomp/src/Settings.cpp, CSettings::assignDefault: SHADOWRESOLUTION 128 << (detail - 1), down to
// 128). Normal leaves the game's values.
//
// So far the shadow resolution only: the width and height of the game client's shadow (and light)
// projector render textures, a third of the guest thread and a share of the GPU's frame in the
// POCO X7's fight (docs/performance-profile.md).

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace torchlight::hooks {

enum class Quality { kNormal, kLow, kVeryLow };

constexpr std::optional<Quality> ParseQuality(std::string_view s) {
  if (s == "normal") return Quality::kNormal;
  if (s == "low") return Quality::kLow;
  if (s == "very_low") return Quality::kVeryLow;
  return std::nullopt;
}

constexpr std::string_view QualityName(Quality q) {
  switch (q) {
    case Quality::kLow: return "low";
    case Quality::kVeryLow: return "very_low";
    default: return "normal";
  }
}

// SHADOWRESOLUTION for the level; none keeps the game's (512).
constexpr std::optional<uint32_t> QualityShadowResolution(Quality q) {
  switch (q) {
    case Quality::kLow: return 256;
    case Quality::kVeryLow: return 128;
    default: return std::nullopt;
  }
}

}  // namespace torchlight::hooks
