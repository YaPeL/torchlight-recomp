// MakeProgramKey / InputsFromKey: every program key comes back to inputs that make the same key,
// so the precompilation (tl_backend_precompile_programs) generates the program play would.

#include <cstdio>
#include <cstring>
#include <string>

#include "backend/program_key.h"

namespace {
int g_failures = 0;
void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++g_failures;
  }
}

using torchlight::backend::InputsFromKey;
using torchlight::backend::MakeProgramKey;
using torchlight::backend::ProgramInputs;
using torchlight::backend::ProgramKey;
using torchlight::backend::UnitDesc;
using torchlight::backend::UnitKey;

// A field added to the key has to be decoded by InputsFromKey and given a value other than its
// default in Full() below; then update these sizes.
static_assert(sizeof(UnitKey) == 12 + 10 * sizeof(float),
              "UnitKey changed: decode the new field in InputsFromKey and set it in Full()");
static_assert(sizeof(ProgramKey) == 8 + TL_MAX_STAGES * sizeof(UnitKey),
              "ProgramKey changed: decode the new field in InputsFromKey and set it in Full()");

uint32_t g_seed = 12345;
uint32_t Next(uint32_t range) {
  g_seed = g_seed * 1664525u + 1013904223u;
  return (g_seed >> 8) % range;
}

// Every field the key holds, away from its default; `variant` varies the choices.
void Full(uint32_t variant, ProgramInputs& f, UnitDesc* units, uint32_t& unit_count, bool* cube) {
  f = ProgramInputs();
  f.lighting = variant % 2 == 0;
  f.tracking = variant % 3 ? Ogre::TVC_DIFFUSE : Ogre::TVC_NONE;
  f.light = true;
  f.fog = variant % 4 != 1;
  f.skin_bones = variant % 5 ? 20 + variant : 0;
  f.skin_weights = f.skin_bones ? 1 + variant % 4 : 0;
  unit_count = TL_MAX_STAGES - variant % 3;
  for (uint32_t u = 0; u < unit_count; ++u) {
    UnitDesc& d = units[u];
    d = UnitDesc();
    d.present = (u + variant) % 5 != 3;  // a gap below used units
    cube[u] = (u + variant) % 3 == 0;
    if (!d.present) continue;
    d.texgen = Ogre::TexCoordCalcMethod(Next(Ogre::TEXCALC_PROJECTIVE_TEXTURE + 1));
    d.coord_set = Next(8);
    d.has_matrix = Next(2) != 0;
    d.guest_texgen = UnitDesc::Guest(Next(3));
    d.colour.blendType = Ogre::LBT_COLOUR;
    d.colour.operation = Ogre::LayerBlendOperationEx(Next(Ogre::LBX_DOTPRODUCT + 1));
    d.colour.source1 = Ogre::LayerBlendSource(Next(Ogre::LBS_SPECULAR + 1));
    d.colour.source2 = Next(2) ? Ogre::LBS_MANUAL : Ogre::LayerBlendSource(Next(5));
    d.colour.colourArg1 = Ogre::ColourValue(0.1f * u, 0.2f, 0.3f, 0.4f + 0.01f * variant);
    d.colour.colourArg2 = Ogre::ColourValue(0.5f, 0.6f * u, 0.7f, 0.8f);
    d.alpha.blendType = Ogre::LBT_ALPHA;
    d.alpha.operation = Ogre::LayerBlendOperationEx(Next(Ogre::LBX_DOTPRODUCT + 1));
    d.alpha.source1 = Next(2) ? Ogre::LBS_MANUAL : Ogre::LayerBlendSource(Next(5));
    d.alpha.source2 = Ogre::LayerBlendSource(Next(Ogre::LBS_SPECULAR + 1));
    d.alpha.alphaArg1 = 0.25f + 0.01f * u;
    d.alpha.alphaArg2 = 0.75f;
  }
}
}  // namespace

int main() {
  for (uint32_t variant = 0; variant < 200; ++variant) {
    ProgramInputs f;
    UnitDesc units[TL_MAX_STAGES];
    uint32_t count = 0;
    bool cube[TL_MAX_STAGES] = {};
    Full(variant, f, units, count, cube);
    const std::string key = MakeProgramKey(cube, f, units, count);

    ProgramInputs back;
    UnitDesc back_units[TL_MAX_STAGES];
    uint32_t back_count = 0;
    bool back_cube[TL_MAX_STAGES] = {};
    const bool ok = InputsFromKey(key, back, back_units, back_count, back_cube);
    Check(ok, "a key made by MakeProgramKey decodes");
    if (!ok) break;
    Check(MakeProgramKey(back_cube, back, back_units, back_count) == key,
          "the decoded inputs make the same key");
    Check(back.lighting == f.lighting && back.tracking == f.tracking && back.fog == f.fog &&
              back.skin_bones == f.skin_bones && back.skin_weights == f.skin_weights &&
              back_count == count,
          "the draw's keyed fields come back");
    for (uint32_t u = 0; u < count; ++u) {
      Check(back_units[u].present == units[u].present, "each unit's presence comes back");
      if (!units[u].present) continue;
      const UnitDesc& a = units[u];
      const UnitDesc& b = back_units[u];
      Check(back_cube[u] == cube[u] && b.texgen == a.texgen && b.coord_set == a.coord_set &&
                b.has_matrix == a.has_matrix && b.guest_texgen == a.guest_texgen &&
                b.colour.operation == a.colour.operation &&
                b.colour.source1 == a.colour.source1 && b.colour.source2 == a.colour.source2 &&
                b.alpha.operation == a.alpha.operation && b.alpha.source1 == a.alpha.source1 &&
                b.alpha.source2 == a.alpha.source2,
            "each unit's keyed fields come back");
    }
  }

  ProgramInputs f;
  UnitDesc units[TL_MAX_STAGES];
  uint32_t count = 0;
  bool cube[TL_MAX_STAGES] = {};
  Check(!InputsFromKey("short", f, units, count, cube), "a key of another size is refused");
  ProgramKey k;
  std::memset(&k, 0, sizeof(k));
  k.unit_count = TL_MAX_STAGES + 1;
  Check(!InputsFromKey(std::string(reinterpret_cast<const char*>(&k), sizeof(k)), f, units, count,
                       cube),
        "more units than the backend has are refused");
  return g_failures ? 1 : 0;
}
