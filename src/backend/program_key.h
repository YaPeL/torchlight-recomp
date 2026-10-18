// The structural key of a draw's RTSS program (ProgramKey): what of the draw the generated code
// depends on, packed into bytes the backend keys its programs by and keeps in the program list
// (program_list.h), and the way back from a key to inputs that generate the same program
// (InputsFromKey, for the precompilation). Inside the backend only.

#pragma once

#include <cstdint>
#include <string>

#include <Ogre.h>

#include "backend/backend_api.h"  // TL_MAX_STAGES

namespace torchlight::backend {

// Transform, lighting, fog and skinning of a draw: the values behind its RTSS program
// (ApplyProgram).
struct ProgramInputs {
  Ogre::Matrix4 world = Ogre::Matrix4::IDENTITY;
  Ogre::Matrix4 projection = Ogre::Matrix4::IDENTITY;  // GL clip; the data source flips it for RTs
  // The guest's own clip matrix (world-view-projection), in GL conventions: the position
  // transform. projection * world equals it, but when world is the guest world-view split from it
  // (GuestWorldViewFromWvp) the product carries float error, enough to push geometry the guest
  // places exactly on the near plane (z = 0 in D3D, -w in GL) outside the clip volume.
  Ogre::Matrix4 clip = Ogre::Matrix4::IDENTITY;
  bool lighting = false;
  Ogre::TrackVertexColourType tracking = Ogre::TVC_NONE;
  Ogre::ColourValue diffuse = Ogre::ColourValue::White, emissive = Ogre::ColourValue::Black;
  bool light = false;
  float towards_light[3] = {0, 0, 1};  // view space
  Ogre::ColourValue light_diffuse = Ogre::ColourValue::Black;
  bool fog = false;
  float fog_start = 0, fog_end = 0;
  Ogre::ColourValue fog_colour = Ogre::ColourValue::Black;
  // Skinning (RTSS HardwareSkinning): palette size, influences and the guest's bones (3x4
  // row-major, world space).
  uint32_t skin_bones = 0, skin_weights = 0;
  const float* skin_palette = nullptr;
  // Alpha test (render state; GuestAlphaTest).
  Ogre::CompareFunction alpha_function = Ogre::CMPF_ALWAYS_PASS;
  uint8_t alpha_reference = 0;
};

// What a draw's texture unit does, for the program generation (ProgramKey) and its constants.
struct UnitDesc {
  bool present = false;  // sampled with a texture bound
  Ogre::LayerBlendModeEx colour, alpha;
  Ogre::TexCoordCalcMethod texgen = Ogre::TEXCALC_NONE;
  uint32_t coord_set = 0;
  bool has_matrix = false;
  Ogre::Matrix4 matrix = Ogre::Matrix4::IDENTITY;
  // Coordinates the program computes itself (GuestTexgen); coord_set is then the reserved output
  // set.
  enum class Guest : uint8_t { kNone, kViewNormal2d, kReflect } guest_texgen = Guest::kNone;
};

// A unit's combination as it reaches the RTSS: the operations and sources, and the constant
// arguments where a source reads them. The blend factor is not in the key: only
// LBX_BLEND_MANUAL reads it, and no operation of the API maps to it (ToOgre(tl_combine_op)), so it
// is always the default. An operation that reads it must add it here.
#pragma pack(push, 1)
struct UnitKey {
  uint8_t present, cube, texgen, coord_set, has_matrix, guest_texgen;
  uint8_t colour_op, colour_source1, colour_source2, alpha_op, alpha_source1, alpha_source2;
  float colour_arg1[4], colour_arg2[4], alpha_arg1, alpha_arg2;
};
struct ProgramKey {
  uint8_t lighting, tracking, light_count, fog, unit_count, skin_weights;
  uint16_t skin_bones;
  UnitKey units[TL_MAX_STAGES];
};
#pragma pack(pop)

// The key of the program for `f` and `units` (the first `unit_count`); `unit_cube`: per unit,
// whether its texture is a cube map.
std::string MakeProgramKey(const bool* unit_cube, const ProgramInputs& f, const UnitDesc* units,
                           uint32_t unit_count);

// The inputs MakeProgramKey made `key` from, as far as the program depends on them (what is not in
// the key, the matrices and colours, is written per draw), and each unit's texture type into
// `unit_cube`: false when `key` is not a ProgramKey this build makes the same way back.
bool InputsFromKey(const std::string& key, ProgramInputs& f, UnitDesc* units, uint32_t& unit_count,
                   bool* unit_cube);

}  // namespace torchlight::backend
