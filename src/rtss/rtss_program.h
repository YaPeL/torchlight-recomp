// Reading of the guest's RTSS-generated HLSL programs (OGRE 1.7 RTShaderSystem, Runic build).
//
// The guest compiles every program from source at load time (createGpuProgram, see guest_abi)
// and the capture records that source. The programs are straight-line sequences of FFPLib calls,
// so what each one does can be read from the calls, and where each global lives in the constant
// registers follows from the declarations.
//
// No OGRE, PPC or SDK dependencies.

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace torchlight::rtss {

struct Global {
  std::string name;
  std::string type;    // HLSL type as declared, e.g. float4x4
  uint32_t array = 1;  // element count of an array declaration
  // Constant register range, valid after ResolveRegisters.
  uint32_t first_register = 0;
  uint32_t registers = 0;
};

struct Parameter {  // argument of main
  bool output = false;
  std::string type, name, semantic;
};

struct Call {
  std::string function;
  std::vector<std::string> args;  // as written, whitespace trimmed
};

struct Program {
  // Globals referenced by main, sorted by name: the order of their constant registers.
  std::vector<Global> globals;
  std::vector<Parameter> parameters;
  std::vector<Call> calls;  // statements of main that are function calls, in order
};

// Parses the source as written by the RTSS HLSL writer. Returns nullopt when the expected
// sections ("GLOBAL PARAMETERS", the main function) are missing.
std::optional<Program> Parse(std::string_view source);

// Placement of an auto constant in the uploaded constants (floats, physical index / 4 is the
// register).
struct AutoPlacement {
  uint32_t physical_index = 0;
  uint32_t element_count = 0;
};

// Assigns constant registers to `program.globals`. The guest compiler's table (and the physical
// indices the guest D3D9 render system derives from it) has the referenced globals sorted by
// name, packed from register 0, rows as registers (column_major_matrices = false), and drops
// matrix rows a program never reads. The size of a global backed by an auto constant is therefore
// the auto's element count; any other global takes its declared size. Checked against the 42
// programs of the v13 captures: every auto constant starts where this placement puts a global.
// Returns false (with `error`) when an auto constant does not start at a global.
bool ResolveRegisters(Program& program, std::span<const AutoPlacement> autos, std::string* error);

const Global* FindGlobal(const Program& program, std::string_view name);

// Name of the variable in an argument: "light_position_view_space0.xyz" -> its global name.
std::string BaseName(std::string_view arg);

// ---- what a program does --------------------------------------------------------------------

enum class VertexColour {
  kNone,         // no colour output: the fragment stage uses a constant
  kVertexInput,  // the vertex colour, unchanged
  kSceneColour,  // derived scene colour (ambient + emissive), plus lights when lit
};

struct DirectionalLight {
  std::string direction;  // view-space direction towards the light (global name)
  std::string diffuse;    // diffuse colour (global name)
  bool diffuse_from_vertex = false;  // the diffuse colour is modulated by the vertex colour
};

// How a vertex program fills one texture coordinate output (FFPLib texturing functions).
enum class TexCoordSource {
  kInput,         // FFP_Assign(iTexcoord, out): an input set unchanged
  kTransformed,   // FFP_TransformTexCoord(matrix, iTexcoord, out): (matrix * (u, v, 0, 1)).xy
  kProjective,    // FFP_GenerateTexCoord_Projection(world, projector, pos, out):
                  //   projector * world * pos, (x, y, w) sampled as (x / w, y / w)
  kEnvMapNormal,  // FFP_GenerateTexCoord_EnvMap_Normal(worldIT, view, [matrix,] normal, out):
                  //   view-space normal, [matrix * (n, 1)]
  kEnvMapReflect, // FFP_GenerateTexCoord_EnvMap_Reflect(world, worldIT, view, [matrix,] normal, pos,
                  //   out): world-space reflection of the eye direction, z negated, [matrix *
                  //   (r, 1)]
  kEnvMapSphere,  // FFP_GenerateTexCoord_EnvMap_Sphere(world, view, [matrix,] normal, out):
                  //   (n.x / 2 + 0.5, -n.y / 2 + 0.5) of the view-space normal, [matrix * (s, t, 0, 0)]
};

struct TexCoordOutput {
  std::string semantic;  // e.g. TEXCOORD1
  TexCoordSource source = TexCoordSource::kInput;
  uint32_t input_set = 0;  // kInput / kTransformed: input TEXCOORD index
  std::string matrix;      // texture matrix global (kTransformed, optional for env maps)
  std::string world, projector;  // kProjective globals; world also for kEnvMapReflect
};

// Runic's hardware skinning (SGX_HardwareSkinning, not part of OGRE 1.7.0; uses the float3x4
// FFP_Transform overloads of the game's FFPLib_Transform.hlsl). Per influence k:
//   acc += weights[weight_lane] * (bones[indices[index_lane]] * (pos, 1), 1)
// with the same influences for the normal (3x3 part). acc is a world-space position: the program
// clips it with `position_matrix` and writes inverse_world * acc back into its position input,
// which the rest of the program (fog, texgen) uses as the object-space position.
struct Skinning {
  std::string bones;  // float3x4 array global
  struct Influence {
    uint32_t index_lane = 0, weight_lane = 0;
  };
  std::vector<Influence> influences;
  std::string position_matrix;  // world-space skinned position -> clip
  std::string inverse_world;    // world-space skinned position -> object space (may be empty)
};

struct VertexFeatures {
  VertexColour colour = VertexColour::kNone;
  std::string scene_colour;               // global holding the base colour (kSceneColour)
  std::vector<DirectionalLight> lights;   // FFP_Light_Directional_Diffuse calls
  std::vector<std::string> unsupported;   // calls this reading does not translate
  bool fog_depth = false;                 // FFP_PixelFog_Depth: fog distance is clip w
  std::vector<TexCoordOutput> texcoords;
  std::optional<Skinning> skinning;
  std::string position_matrix;  // matrix that writes the clip position (non-skinned programs)
};

// One texture stage of a fragment program (the RTSS FFPTexturing sequence: sample, then a colour
// and an alpha operation on source1 / source2 into the output colour).
enum class Operand { kTexture, kCurrent, kDiffuse, kConstant };
enum class Operation {
  kSource1, kSource2, kModulate, kModulateX2, kModulateX4, kAdd, kAddSigned, kAddSmooth,
  kSubtract
};
struct Combine {
  Operation operation = Operation::kModulate;
  Operand source1 = Operand::kTexture, source2 = Operand::kCurrent;
  std::array<float, 4> constant1{}, constant2{};  // kConstant operands
};
struct TextureStage {
  uint32_t sampler = 0;        // gTextureSamplerN: texture unit N
  std::string coord_semantic;  // TEXCOORDn of the coordinate it samples with
  bool projective = false;     // FFP_SampleTextureProj
  Combine colour, alpha;
};

struct FragmentFeatures {
  bool uses_colour = false;  // reads the interpolated colour
  bool fog_linear = false;   // FFP_PixelFog_Linear
  std::string fog_params, fog_colour;  // globals: (unused, unused, end, 1 / (end - start)), colour
  std::vector<TextureStage> stages;    // in sampling order
  std::vector<std::string> unsupported;
};

VertexFeatures AnalyseVertex(const Program& program);
FragmentFeatures AnalyseFragment(const Program& program);

}  // namespace torchlight::rtss
