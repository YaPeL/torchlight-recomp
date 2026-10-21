// The game's own settings (Runic's CSettings), as on the PC (OpenTorchlight, PC decompilation,
// decomp/src/Settings.cpp: properties registered with a name and a default; their index kept in a
// global per property; values in one vector per type).

#pragma once

#include <cstdint>

#include "guest_abi/guest_functions.h"
#include "guest_abi/ogre_layout.h"

namespace torchlight::guest_abi::game_settings {

// [confirmed] The settings' constructor (this in r3, returned in r3; its one caller is
// sub_823DD110 @0x823DD1B8): registers about 130 properties in the PC's order
// through GetIntPropertyIndex 0x82398428 (name r4, default r5, saved r6) and GetFloatPropertyIndex
// 0x823984D8 (default f1), storing each index in a global: the PC's defaults in the same places
// (window and resolution 800x600, MAX_FPS 200, MAX_PARTICLES 5000, SHADOWS_ENABLED 1,
// SHADOWS_DETAIL 4, ..., SHADOWRESOLUTION 512).
inline constexpr functions::GuestFunction kConstructor{0x823DEF18, Confidence::kConfirmed};

// [confirmed] The console's fixed values (the PC's CSettings::assignDefault picks them by the
// machine's detail; this one by none): settings in r3; one caller, sub_82209320 @0x82209534, after
// the constructor. Through SetInt 0x821FA030 (settings r3, index r4, value r5) it sets
// LIGHTING_ENABLED 1, SHADOWS_ENABLED 1 and SHADOWRESOLUTION 512 (@ its loads of each index), and
// through SetFloat 0x821FA090 the particle rates. A value written after the constructor is
// overwritten here: the quality level goes after it.
inline constexpr functions::GuestFunction kAssignDefaults{0x823E1040, Confidence::kConfirmed};

// [confirmed] Values: GetIntPropertyIndex push_backs the default at this+108 (std::vector<int>,
// @0x82398480..) and returns end - 1 as the index; floats likewise at this+124 (0x823984D8).
inline constexpr Field kIntValuesBegin{0x6C, Confidence::kConfirmed};
inline constexpr Field kIntValuesEnd{0x70, Confidence::kConfirmed};
inline constexpr Field kFloatValuesBegin{0x7C, Confidence::kConfirmed};
inline constexpr Field kFloatValuesEnd{0x80, Confidence::kConfirmed};

// [confirmed] Index globals (the constructor's stores after each registration).
// SHADOWRESOLUTION, default 512, not saved (r6 = 0): the 58th registration (stw @-29048 of
// 0x83560000). Read by the game client's projector setup (0x82210148): its value is the width and
// height of the shadow render texture it creates (createManual 0x8221EFE8, usage 0x200), when
// SHADOWS_ENABLED (index at 0x83558F1C) is set.
inline constexpr uint32_t kShadowResolutionIndex = 0x83558E88;
inline constexpr uint32_t kShadowsEnabledIndex = 0x83558F1C;
// PARTICLEFPS and PARTICLE_EMIT_PCT (float values; the 8th and 9th registrations, stw @-29028 and
// @-29032), which the console defaults set through SetFloat 0x821FA090 and the game reads after
// them (PARTICLE_EMIT_PCT @0x822068A0, with MAX_PARTICLES; PARTICLEFPS in 0x8234BE20). On the PC
// they are the particle system's update rate and the percentage of looping emitters' particles
// released (settingsmenu.cpp: setParticleFPS, setEmitterLoopingReleasePCT(pct / 100)).
inline constexpr uint32_t kParticleFpsIndex = 0x83558E9C;
inline constexpr uint32_t kParticleEmitPercentIndex = 0x83558E98;

}  // namespace torchlight::guest_abi::game_settings
