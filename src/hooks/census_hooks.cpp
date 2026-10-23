// Diagnostics: how many of some guest objects exist, logged with every 10 s frame summary
// ("census: ..."), to see what accumulates over a long fight (the 10 waves of fight5.txt grow the
// guest's frame time from about 22 to 35 ms with the same 21 enemies alive per wave).
//
// Counted by wrapping each class's constructor and destructor (guest_abi/guest_functions.h,
// census): +1 when a constructor returns, -1 when a destructor starts. A derived class's
// constructor runs its base's, so "units" (CBaseUnit) counts the characters and the items both.
// The option is read once, before the guest runs, so the counts cover the whole process. Compiled only
// with the build flag TORCHLIGHT_DIAGNOSTICS (CMakeLists.txt; off in releases, where no guest
// function is wrapped); then off by default (--diag_object_census), each wrapper only calls the
// guest function.

#include <atomic>
#include <cstdint>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include "guest_abi/guest_functions.h"
#include "live/frame_timing.h"

REXCVAR_DEFINE_BOOL(diag_object_census, false, "Torchlight",
                    "Diagnostics: count some guest objects (characters, items, scene nodes, "
                    "entities, particle systems, lights, billboard sets) and log the counts every "
                    "10 s with the frame summary");

namespace {

namespace census = torchlight::guest_abi::functions::census;

enum Kind { kCharacter, kBaseUnit, kItem, kLevel, kSceneNode, kEntity, kParticles, kLight,
            kBillboards, kKinds };
constexpr const char* kNames[kKinds] = {"characters", "units",          "items",
                                        "levels",     "scene_nodes",    "entities",
                                        "particle_systems", "lights",   "billboard_sets"};
std::atomic<int64_t> g_live[kKinds];
std::atomic<int64_t> g_made[kKinds];

bool Enabled() {
  static const bool enabled = REXCVAR_GET(diag_object_census);
  return enabled;
}

void LogCensus() {
  std::string line = "census:";
  for (int k = 0; k < kKinds; ++k) {
    line += " ";
    line += kNames[k];
    line += "=" + std::to_string(g_live[k].load(std::memory_order_relaxed)) + " (+" +
            std::to_string(g_made[k].exchange(0, std::memory_order_relaxed)) + ")";
  }
  REXLOG_INFO("{}", line);
}

struct Install {
  Install() { torchlight::live::AddSummaryHook([] { if (Enabled()) LogCensus(); }); }
} g_install;

void Made(Kind k) {
  if (!Enabled()) return;
  g_live[k].fetch_add(1, std::memory_order_relaxed);
  g_made[k].fetch_add(1, std::memory_order_relaxed);
}
void Gone(Kind k) {
  if (Enabled()) g_live[k].fetch_sub(1, std::memory_order_relaxed);
}

}  // namespace

#define CENSUS_CHECK(entry, addr) \
  static_assert(census::entry.address == 0x##addr##u, "guest_functions mismatch")

// A constructor: the guest's, then +1. A destructor: -1, then the guest's.
#define CENSUS_CTOR(entry, addr, kind)      \
  CENSUS_CHECK(entry, addr);                \
  REX_EXTERN(__imp__sub_##addr);            \
  REX_FUNC(sub_##addr) {                    \
    __imp__sub_##addr(ctx, base);           \
    Made(kind);                             \
  }
#define CENSUS_DTOR(entry, addr, kind)      \
  CENSUS_CHECK(entry, addr);                \
  REX_EXTERN(__imp__sub_##addr);            \
  REX_FUNC(sub_##addr) {                    \
    Gone(kind);                             \
    __imp__sub_##addr(ctx, base);           \
  }

extern "C" {
CENSUS_CTOR(kCharacterCtor, 82283398, kCharacter)
CENSUS_DTOR(kCharacterDtor, 82284290, kCharacter)
CENSUS_CTOR(kBaseUnitCtor, 8227FB98, kBaseUnit)
CENSUS_DTOR(kBaseUnitDtor, 8227FDB8, kBaseUnit)
CENSUS_CTOR(kItemCtor, 822C7A48, kItem)
CENSUS_DTOR(kItemDtor, 822C7B48, kItem)
CENSUS_CTOR(kLevelCtor, 822E8E30, kLevel)
CENSUS_DTOR(kLevelDtor, 822E9278, kLevel)
CENSUS_CTOR(kSceneNodeCtor, 82450A40, kSceneNode)
CENSUS_CTOR(kSceneNodeCtorNamed, 82450B80, kSceneNode)
CENSUS_DTOR(kSceneNodeDtor, 82450C68, kSceneNode)
CENSUS_CTOR(kEntityCtor, 82458400, kEntity)
CENSUS_DTOR(kEntityDtor, 82458AC0, kEntity)
CENSUS_CTOR(kParticleSystemCtor, 824F9A28, kParticles)
CENSUS_DTOR(kParticleSystemDtor, 824F9D68, kParticles)
CENSUS_CTOR(kLightCtor, 82455E68, kLight)
CENSUS_CTOR(kLightCtorNamed, 82456078, kLight)
CENSUS_DTOR(kLightDtor, 82456210, kLight)
CENSUS_CTOR(kBillboardSetCtor, 824798C0, kBillboards)
CENSUS_DTOR(kBillboardSetDtor, 82479B08, kBillboards)
}  // extern "C"
