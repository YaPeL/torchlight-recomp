#include "backend/program_key.h"

#include <cstring>

namespace torchlight::backend {

std::string MakeProgramKey(const bool* unit_cube, const ProgramInputs& f, const UnitDesc* units,
                           uint32_t unit_count) {
  ProgramKey k;
  std::memset(&k, 0, sizeof(k));
  k.lighting = f.lighting;
  k.tracking = uint8_t(f.tracking);
  k.light_count = f.lighting && f.light ? 1 : 0;
  k.fog = f.fog;
  k.unit_count = uint8_t(unit_count);
  k.skin_bones = uint16_t(f.skin_bones);
  k.skin_weights = uint8_t(f.skin_weights);
  for (uint32_t u = 0; u < unit_count; ++u) {
    const UnitDesc& d = units[u];
    UnitKey& uk = k.units[u];
    uk.present = d.present;
    if (!d.present) continue;
    uk.cube = unit_cube[u];
    uk.texgen = uint8_t(d.texgen);
    uk.coord_set = uint8_t(d.coord_set);
    uk.has_matrix = d.has_matrix;
    uk.guest_texgen = uint8_t(d.guest_texgen);
    uk.colour_op = uint8_t(d.colour.operation);
    uk.colour_source1 = uint8_t(d.colour.source1);
    uk.colour_source2 = uint8_t(d.colour.source2);
    uk.alpha_op = uint8_t(d.alpha.operation);
    uk.alpha_source1 = uint8_t(d.alpha.source1);
    uk.alpha_source2 = uint8_t(d.alpha.source2);
    if (d.colour.source1 == Ogre::LBS_MANUAL || d.colour.source2 == Ogre::LBS_MANUAL) {
      const Ogre::ColourValue* args[2] = {&d.colour.colourArg1, &d.colour.colourArg2};
      for (int i = 0; i < 4; ++i) {
        uk.colour_arg1[i] = args[0]->ptr()[i];
        uk.colour_arg2[i] = args[1]->ptr()[i];
      }
    }
    if (d.alpha.source1 == Ogre::LBS_MANUAL || d.alpha.source2 == Ogre::LBS_MANUAL) {
      uk.alpha_arg1 = d.alpha.alphaArg1;
      uk.alpha_arg2 = d.alpha.alphaArg2;
    }
  }
  return std::string(reinterpret_cast<const char*>(&k), sizeof(k));
}

bool InputsFromKey(const std::string& key, ProgramInputs& f, UnitDesc* units, uint32_t& unit_count,
                   bool* unit_cube) {
  if (key.size() != sizeof(ProgramKey)) return false;
  ProgramKey k;
  std::memcpy(&k, key.data(), sizeof(k));
  if (k.unit_count > TL_MAX_STAGES) return false;
  f.lighting = k.lighting != 0;
  f.tracking = Ogre::TrackVertexColourType(k.tracking);
  f.light = k.light_count != 0;
  f.fog = k.fog != 0;
  f.skin_bones = k.skin_bones;
  f.skin_weights = k.skin_weights;
  unit_count = k.unit_count;
  for (uint32_t u = 0; u < unit_count; ++u) {
    const UnitKey& uk = k.units[u];
    UnitDesc& d = units[u];
    d = UnitDesc();
    d.present = uk.present != 0;
    if (!d.present) continue;
    unit_cube[u] = uk.cube != 0;
    d.texgen = Ogre::TexCoordCalcMethod(uk.texgen);
    d.coord_set = uk.coord_set;
    d.has_matrix = uk.has_matrix != 0;
    d.guest_texgen = UnitDesc::Guest(uk.guest_texgen);
    d.colour.blendType = Ogre::LBT_COLOUR;
    d.colour.operation = Ogre::LayerBlendOperationEx(uk.colour_op);
    d.colour.source1 = Ogre::LayerBlendSource(uk.colour_source1);
    d.colour.source2 = Ogre::LayerBlendSource(uk.colour_source2);
    d.colour.colourArg1 = Ogre::ColourValue(uk.colour_arg1[0], uk.colour_arg1[1],
                                            uk.colour_arg1[2], uk.colour_arg1[3]);
    d.colour.colourArg2 = Ogre::ColourValue(uk.colour_arg2[0], uk.colour_arg2[1],
                                            uk.colour_arg2[2], uk.colour_arg2[3]);
    d.alpha.blendType = Ogre::LBT_ALPHA;
    d.alpha.operation = Ogre::LayerBlendOperationEx(uk.alpha_op);
    d.alpha.source1 = Ogre::LayerBlendSource(uk.alpha_source1);
    d.alpha.source2 = Ogre::LayerBlendSource(uk.alpha_source2);
    d.alpha.alphaArg1 = uk.alpha_arg1;
    d.alpha.alphaArg2 = uk.alpha_arg2;
  }
  // Only a key that comes back the same: anything this build would key differently is skipped.
  return MakeProgramKey(unit_cube, f, units, unit_count) == key;
}

}  // namespace torchlight::backend
