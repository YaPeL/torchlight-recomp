// Tests for the RTSS program reader, on programs as the guest generated them (v13 captures).

#include <cstdio>
#include <cstdlib>
#include <string>

#include "rtss/rtss_program.h"

namespace {

using namespace torchlight::rtss;

void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    std::exit(1);
  }
}

// 1871553573_VS: one directional light with the diffuse tracking the vertex colour, projective
// texturing and depth fog.
const char* kVertex = R"HLSL(//                         GLOBAL PARAMETERS
//-----------------------------------------------------------------------------

float4x4	worldviewproj_matrix;
float4x4	inverse_transpose_worldview_matrix;
float4	derived_ambient_light_colour;
float4	surface_specular_colour;
float4	surface_emissive_colour;
float4	derived_scene_colour;
float	surface_shininess;
float4	light_position_view_space0;
float4	light_diffuse1;
float4x4	texture_matrix1;
float4x4	world_matrix;
float4x4	gTexViewProjImageMatrix0;

//-----------------------------------------------------------------------------
// Function Name: main
// Function Desc: Vertex Program Entry point
//-----------------------------------------------------------------------------
void main
	(
	 in float4	iPos_0 : POSITION, 
	 in float4	iColor_0 : COLOR_center, 
	 in float3	iNormal_0 : NORMAL, 
	 in float2	iTexcoord2_0 : TEXCOORD0, 
	 out float4	oPos_0 : POSITION, 
	 out float4	oColor_0 : COLOR, 
	 out float2	oTexcoord2_0 : TEXCOORD0, 
	 out float3	oTexcoord3_1 : TEXCOORD1, 
	 out float	oTexcoord1_2 : TEXCOORD2
	)
{
	float4	lLocalParam_0;

	FFP_Transform(worldviewproj_matrix, iPos_0, oPos_0);

	FFP_Assign(iColor_0, oColor_0);

	FFP_Construct(0.0, 0.0, 0.0, 0.0, lLocalParam_0);

	FFP_Assign(derived_scene_colour, oColor_0);

	FFP_Modulate(iColor_0.xyz, light_diffuse1.xyz, light_diffuse1.xyz);

	FFP_Light_Directional_Diffuse(inverse_transpose_worldview_matrix, iNormal_0, light_position_view_space0.xyz, light_diffuse1.xyz, oColor_0.xyz, oColor_0.xyz);

	FFP_Assign(iTexcoord2_0, oTexcoord2_0);

	FFP_GenerateTexCoord_Projection(world_matrix, gTexViewProjImageMatrix0, iPos_0, oTexcoord3_1);

	FFP_PixelFog_Depth(worldviewproj_matrix, iPos_0, oTexcoord1_2);
}

)HLSL";

// 1064228662_FS: texture x colour, projective texture x4, linear fog.
const char* kFragment = R"HLSL(//                         GLOBAL PARAMETERS
//-----------------------------------------------------------------------------

sampler2D	gTextureSampler0 : register(s0);
sampler2D	gTextureSampler1 : register(s1);
float4	gFogColor0;
float4	gFogParams1;

//-----------------------------------------------------------------------------
// Function Name: main
// Function Desc: Pixel Program Entry point
//-----------------------------------------------------------------------------
void main
	(
	 in float4	oPos_0 : POSITION, 
	 in float4	iColor_0 : COLOR_center, 
	 in float2	iTexcoord2_0 : TEXCOORD0, 
	 in float3	iTexcoord3_1 : TEXCOORD1, 
	 in float	iTexcoord1_2 : TEXCOORD2, 
	 out float4	oColor_0 : COLOR
	)
{
	float4	lLocalParam_0;
	float4	texel;
	float4	source1;
	float4	source2;

	FFP_Construct(0.0, 0.0, 0.0, 0.0, lLocalParam_0);

	FFP_Assign(iColor_0, oColor_0);

	FFP_SampleTexture(gTextureSampler0, iTexcoord2_0, texel);

	FFP_Assign(texel, source1);

	FFP_Assign(iColor_0, source2);

	FFP_Modulate(source1, source2, oColor_0);

	FFP_SampleTextureProj(gTextureSampler1, iTexcoord3_1, texel);

	FFP_Assign(texel, source1);

	FFP_Assign(oColor_0, source2);

	FFP_ModulateX4(source1.xyz, source2.xyz, oColor_0.xyz);

	FFP_Assign(texel, source1);

	FFP_Assign(oColor_0, source2);

	FFP_Modulate(source1.w, source2.w, oColor_0.w);

	FFP_Add(oColor_0.xyz, lLocalParam_0.xyz, oColor_0.xyz);

	FFP_PixelFog_Linear(iTexcoord1_2, gFogParams1, gFogColor0, oColor_0, oColor_0);
}

)HLSL";

// Runic skinning, two influences (shape of 2104839163_VS).
const char* kSkinned = R"HLSL(
// GLOBAL PARAMETERS
float3x4	world_matrix_array_3x4[64];
float4x4	inverse_world_matrix;
float4x4	viewproj_matrix;
float4x4	worldviewproj_matrix;
// Function Name: main
void main
	(
	 in float4	iPos_0 : POSITION,
	 in float3	iNormal_0 : NORMAL,
	 in float4	iBlendIndices_0 : BLENDINDICES,
	 in float4	iBlendWeights_0 : BLENDWEIGHT,
	 out float4	oPos_0 : POSITION,
	 out float	oTexcoord1_0 : TEXCOORD0
	)
{
	float4	lLocalParam_0;
	float4	TempVal4;
	FFP_Transform(world_matrix_array_3x4[iBlendIndices_0[3]], iPos_0, TempVal4.xyz);
	FFP_Assign(1.0, TempVal4.w);
	FFP_Modulate(TempVal4, iBlendWeights_0[0], TempVal4);
	FFP_Assign(TempVal4, lLocalParam_0);
	FFP_Transform(world_matrix_array_3x4[iBlendIndices_0[2]], iPos_0, TempVal4.xyz);
	FFP_Assign(1.0, TempVal4.w);
	FFP_Modulate(TempVal4, iBlendWeights_0[1], TempVal4);
	FFP_Add(lLocalParam_0, TempVal4, lLocalParam_0);
	FFP_Transform(inverse_world_matrix, lLocalParam_0, iPos_0);
	FFP_Transform(viewproj_matrix, lLocalParam_0, oPos_0);
	FFP_PixelFog_Depth(worldviewproj_matrix, iPos_0, oTexcoord1_0);
}
)HLSL";


// Guest program 1463271133_VS (town capture v19town): reflection texgen with a texture matrix.
const char* kReflect = R"HLSL(//                         GLOBAL PARAMETERS
//-----------------------------------------------------------------------------

float4x4	worldviewproj_matrix;
float4x4	texture_matrix;
float4x4	world_matrix;
float4x4	inverse_transpose_world_matrix;
float4x4	view_matrix;

//-----------------------------------------------------------------------------
// Function Name: main
// Function Desc: Vertex Program Entry point
//-----------------------------------------------------------------------------
void main
	(
	 in float4	iPos_0 : POSITION, 
	 in float3	iNormal_0 : NORMAL, 
	 in float2	iTexcoord2_0 : TEXCOORD0, 
	 out float4	oPos_0 : POSITION, 
	 out float3	oTexcoord3_0 : TEXCOORD0, 
	 out float2	oTexcoord2_1 : TEXCOORD1, 
	 out float	oTexcoord1_2 : TEXCOORD2
	)
{
	float4	lLocalParam_0;

	FFP_Transform(worldviewproj_matrix, iPos_0, oPos_0);

	FFP_Construct(1.0, 1.0, 1.0, 1.0, lLocalParam_0);

	FFP_GenerateTexCoord_EnvMap_Reflect(world_matrix, inverse_transpose_world_matrix, view_matrix, texture_matrix, iNormal_0, iPos_0, oTexcoord3_0);

	FFP_Assign(iTexcoord2_0, oTexcoord2_1);

	FFP_PixelFog_Depth(worldviewproj_matrix, iPos_0, oTexcoord1_2);
}
)HLSL";

// Guest program 979592280_FS (town capture v26fade): an object fading in, its alpha scaled by a
// one-scalar FFP_Construct (FFPLib_Common fills all four components).
const char* kFade = R"HLSL(//                         GLOBAL PARAMETERS
//-----------------------------------------------------------------------------

sampler2D	gTextureSampler0 : register(s0);
float4	gFogColor0;
float4	gFogParams1;

//-----------------------------------------------------------------------------
// Function Name: main
// Function Desc: Pixel Program Entry point
//-----------------------------------------------------------------------------
void main
	(
	 in float4	oPos_0 : POSITION, 
	 in float4	iColor_0 : COLOR_center, 
	 in float2	iTexcoord2_0 : TEXCOORD0, 
	 in float	iTexcoord1_2 : TEXCOORD2, 
	 out float4	oColor_0 : COLOR
	)
{
	float4	lLocalParam_0;
	float4	texel;
	float4	source1;
	float4	source2;

	FFP_Construct(0.0, 0.0, 0.0, 0.0, lLocalParam_0);

	FFP_Assign(iColor_0, oColor_0);

	FFP_SampleTexture(gTextureSampler0, iTexcoord2_0, texel);

	FFP_Assign(texel, source1);

	FFP_Assign(iColor_0, source2);

	FFP_Modulate(source1.xyz, source2.xyz, oColor_0.xyz);

	FFP_Assign(texel, source1);

	FFP_Construct(0.454545, source2);

	FFP_Modulate(source1.w, source2.w, oColor_0.w);

	FFP_Add(oColor_0.xyz, lLocalParam_0.xyz, oColor_0.xyz);

	FFP_PixelFog_Linear(iTexcoord1_2, gFogParams1, gFogColor0, oColor_0, oColor_0);
}
)HLSL";
}  // namespace

int main() {
  auto vs = Parse(kVertex);
  Check(vs.has_value(), "vertex program parses");
  // Unused declarations (surface_*, texture_matrix1, ...) are not part of the table.
  Check(vs->globals.size() == 7, "referenced globals only");
  Check(vs->globals.front().name == "derived_scene_colour", "sorted by name");
  // Auto constants as uploaded: derived_scene_colour @0x4, inverse_transpose_worldview @20x12,
  // world @40x16, worldviewproj @56x16.
  AutoPlacement autos[] = {{0, 4}, {20, 12}, {40, 16}, {56, 16}};
  std::string error;
  Check(ResolveRegisters(*vs, autos, &error), "registers resolve");
  Check(FindGlobal(*vs, "gTexViewProjImageMatrix0")->first_register == 1, "projector matrix c1");
  Check(FindGlobal(*vs, "inverse_transpose_worldview_matrix")->registers == 3, "trimmed rows");
  Check(FindGlobal(*vs, "light_diffuse1")->first_register == 8, "light diffuse c8");
  Check(FindGlobal(*vs, "light_position_view_space0")->first_register == 9, "light direction c9");
  Check(FindGlobal(*vs, "worldviewproj_matrix")->first_register == 14, "wvp c14");
  AutoPlacement misplaced[] = {{8, 4}};  // register 2: inside the projector matrix
  Check(!ResolveRegisters(*vs, misplaced, &error), "an auto between globals is rejected");

  VertexFeatures v = AnalyseVertex(*vs);
  Check(v.colour == VertexColour::kSceneColour && v.scene_colour == "derived_scene_colour",
        "scene colour base");
  Check(v.lights.size() == 1 && v.lights[0].direction == "light_position_view_space0" &&
            v.lights[0].diffuse == "light_diffuse1" && v.lights[0].diffuse_from_vertex,
        "directional light tracking the vertex colour");
  Check(v.fog_depth && v.unsupported.empty(), "depth fog");

  auto fs = Parse(kFragment);
  Check(fs.has_value(), "fragment program parses");
  Check(ResolveRegisters(*fs, {}, &error), "fragment registers");
  Check(FindGlobal(*fs, "gFogColor0")->first_register == 0 &&
            FindGlobal(*fs, "gFogParams1")->first_register == 1,
        "fog constants c0, c1");
  FragmentFeatures f = AnalyseFragment(*fs);
  Check(f.uses_colour && f.fog_linear && f.fog_params == "gFogParams1" &&
            f.fog_colour == "gFogColor0",
        "linear fog");
  Check(v.texcoords.size() == 2 && v.texcoords[0].source == TexCoordSource::kInput &&
            v.texcoords[0].semantic == "TEXCOORD0" && v.texcoords[0].input_set == 0 &&
            v.texcoords[1].source == TexCoordSource::kProjective &&
            v.texcoords[1].semantic == "TEXCOORD1" && v.texcoords[1].world == "world_matrix" &&
            v.texcoords[1].projector == "gTexViewProjImageMatrix0",
        "texture coordinates: input set and projection");
  Check(f.unsupported.empty() && f.stages.size() == 2, "two texture stages");
  const TextureStage& s0 = f.stages[0];
  Check(s0.sampler == 0 && !s0.projective && s0.coord_semantic == "TEXCOORD0" &&
            s0.colour.operation == Operation::kModulate &&
            s0.colour.source1 == Operand::kTexture && s0.colour.source2 == Operand::kDiffuse &&
            s0.alpha.operation == Operation::kModulate,
        "stage 0: texture x diffuse");
  const TextureStage& s1 = f.stages[1];
  Check(s1.sampler == 1 && s1.projective && s1.coord_semantic == "TEXCOORD1" &&
            s1.colour.operation == Operation::kModulateX4 &&
            s1.colour.source1 == Operand::kTexture && s1.colour.source2 == Operand::kCurrent &&
            s1.alpha.operation == Operation::kModulate,
        "stage 1: projected texture x4, alpha modulate");
  Check(!v.skinning && v.position_matrix == "worldviewproj_matrix", "not skinned");
  auto skinned = Parse(kSkinned);
  Check(skinned.has_value(), "skinned program parses");
  VertexFeatures sv = AnalyseVertex(*skinned);
  Check(sv.skinning && sv.skinning->bones == "world_matrix_array_3x4" &&
            sv.skinning->influences.size() == 2 && sv.skinning->influences[0].index_lane == 3 &&
            sv.skinning->influences[0].weight_lane == 0 &&
            sv.skinning->influences[1].index_lane == 2 &&
            sv.skinning->influences[1].weight_lane == 1 &&
            sv.skinning->position_matrix == "viewproj_matrix" &&
            sv.skinning->inverse_world == "inverse_world_matrix",
        "Runic skinning: index lane 3 - k with weight lane k");
  auto reflect = Parse(kReflect);
  Check(reflect.has_value(), "reflection program parses");
  VertexFeatures rv = AnalyseVertex(*reflect);
  Check(rv.unsupported.empty() && rv.texcoords.size() == 2 &&
            rv.texcoords[0].source == TexCoordSource::kEnvMapReflect &&
            rv.texcoords[0].semantic == "TEXCOORD0" && rv.texcoords[0].world == "world_matrix" &&
            rv.texcoords[0].matrix == "texture_matrix",
        "reflection texgen: world and texture matrix");
  auto fade = Parse(kFade);
  Check(fade.has_value(), "fade program parses");
  FragmentFeatures fade_f = AnalyseFragment(*fade);
  Check(fade_f.stages.size() == 1 && fade_f.stages[0].alpha.source2 == Operand::kConstant &&
            fade_f.stages[0].alpha.constant2[3] > 0.4545f &&
            fade_f.stages[0].alpha.constant2[3] < 0.4546f,
        "one-scalar FFP_Construct fills every component (fade alpha)");
  std::printf("rtss test: ok\n");
  return 0;
}
