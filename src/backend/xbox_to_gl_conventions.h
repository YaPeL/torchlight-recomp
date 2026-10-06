// The one place where Xbox 360 / D3D9 conventions are turned into what the GL backend needs.
//
// Every difference between the guest (OGRE 1.7 D3D9 on Xenos, Runic build) and the host (OGRE
// 14 GL) is handled here, by name. Nothing else in the backend corrects for them.
//
// Matrices: draws with a guest vertex program use the uploaded world-view-projection. Lighting,
// fog and texgen need view space, so the backend splits it with the projection the guest set
// (GuestWorldViewFromWvp): host world = guest world-view, host view = identity, host projection =
// the guest projection. Without a view space the uploaded matrix is the host projection.

#ifndef TORCHLIGHT_BACKEND_XBOX_TO_GL_CONVENTIONS_H
#define TORCHLIGHT_BACKEND_XBOX_TO_GL_CONVENTIONS_H

#include <stddef.h>
#include <stdint.h>

namespace torchlight {
namespace backend {

// --- Byte order --------------------------------------------------------------------------------

// Xenos fetch endian swap modes (2 bits of the fetch constant).
enum FetchEndian { kEndianNone = 0, kEndian8In16 = 1, kEndian8In32 = 2, kEndian16In32 = 3 };

// Applies the swap Xenos applies when it fetches the data, so the result reads as little-endian
// lanes, as the GPU sees them. Not a blind byte swap: the mode comes from the fetch constant.
void SwapFetchEndian(uint8_t* data, size_t size, uint32_t mode);

// Guest CPU big-endian indices to host order.
void SwapIndices(uint8_t* data, size_t size, uint32_t index_size);

// Components a vertex element does not provide read as (0, 0, 0, 1) for lanes x, y, z, w (D3D
// vertex fetch default), e.g. a FLOAT3 blend weight read as float4.
inline float MissingVertexComponent(uint32_t lane) { return lane == 3 ? 1.0f : 0.0f; }

// --- Skinning ----------------------------------------------------------------------------------

// Runic's hardware skinning (rtss::Skinning) blends influence k with blend index lane
// index_lane[k] and weight lane weight_lane[k] (the index lanes come reversed from the UBYTE4
// fetch), and clips its accumulator (sum of w * (bone * (p, 1), 1)) so the position is divided by
// the sum of the weights. The RTSS's linear skinning blends influence k with index lane k and
// weight lane k and takes w = 1. Rewrites, per vertex of `data` (stride bytes, after
// SwapFetchEndian), the UBYTE4 indices at index_offset and the weight_components floats at
// weight_offset so that lane k holds Runic's influence k, with the weights divided by their sum
// (lanes from `count` on are zeroed). The blended normal is divided by the sum too (direction
// unchanged). Returns false when the influences do not fit the weight element (count >
// weight_components).
bool BlendLanesForHost(uint8_t* data, size_t size, uint32_t stride, uint32_t index_offset,
                       uint32_t weight_offset, uint32_t weight_components, uint32_t count,
                       const uint8_t index_lane[4], const uint8_t weight_lane[4]);

// --- Vertex colour -----------------------------------------------------------------------------

// D3DCOLOR (ARGB in a little-endian dword after the fetch swap: bytes B, G, R, A) to the GL
// vertex colour layout OGRE GL expects for VET_COLOUR_ABGR (bytes R, G, B, A).
void VertexColourD3DToGl(uint8_t* bgra_bytes);

// --- Clip space and depth ----------------------------------------------------------------------

// The guest's uploaded world-view-projection maps depth to D3D [0, 1] (the guest's own inverted
// depth is applied later by its viewport, which the backend does not reproduce). GL wants
// [-1, 1]: row2' = 2 * row2 - row3. Row-major, column-vector convention (clip = M * v), like
// OGRE's Matrix4.
void GuestClipToGl(const float guest[16], float gl[16]);

// Fixed-function draws: the guest D3D9 render system turns OGRE's right-handed view into a
// left-handed one by negating view row 2 before handing it to D3D (D3D9RenderSystem::_setViewMatrix,
// OgreD3D9RenderSystem.cpp:1638-1641; guest 0x821BAC40 negates the four floats @0x821BAC70..
// @0x821BAC84). The projection it receives already expects that view, so the guest clip matrix is
// P * S * V * W with S = diag(1, 1, -1, 1). Output in guest clip conventions (feed it to
// GuestClipToGl).
void GuestFixedFunctionClip(const float world[16], const float view[16],
                            const float projection[16], float clip[16]);

// Programmed draws: splits the uploaded world-view-projection (P * S * V * W, see above) with the
// guest projection setter's matrix P. world_view = (P * S)^-1 * wvp, the right-handed OGRE view
// space the guest's lighting constants are expressed in; projection_with_flip = P * S (guest clip
// conventions). Returns false when P * S is singular or the result is not affine (the draw used
// another projection than the last one set, e.g. a different camera).
bool GuestWorldViewFromWvp(const float wvp[16], const float projection[16], float world_view[16],
                           float projection_with_flip[16]);

// Texture origin (D3D v = 0 at the top): the backend renders every target into an OGRE render
// texture; the backend negates clip y for targets that require texture flipping (backend.cpp,
// ProjectionForTarget) and OGRE flips the cull winding for them (RenderSystem::flipFrontFace). So
// the guest cull mode and invert-winding flag go to the host unchanged, through OGRE's API, and no
// y flip is applied here. Returns the commands::CullMode to apply on the host.
uint8_t CullModeForHost(uint8_t guest_cull_mode, bool invert_winding);

// Texture coordinate generation. The guest computes these in its programs (FFPLib):
// - Projective (FFP_GenerateTexCoord_Projection): projector * world * position, sampled at
//   (x / w, y / w). The projector already maps to D3D image space (v down), which is also the
//   host's texture origin (above), so the matrix is used as uploaded.
// - Env map normal: the view-space normal, the same as GL's normal-map texgen.
// - Env map sphere: the guest uses the view-space normal, (n.x / 2 + 0.5, -n.y / 2 + 0.5). GL's
//   sphere map (OGRE TEXCALC_ENVIRONMENT_MAP) uses the reflection of the eye vector instead,
//   r / (2 * sqrt(r.x^2 + r.y^2 + (r.z + 1)^2)) + 0.5, so it does not match. The backend uses the
//   normal-map texgen with this matrix on (n, 1), followed by the guest's texture matrix applied to
//   (s, t, 0, 0) (row-major, column vector, q forced to 1).
void SphereMapMatrixForHost(const float* guest_texture_matrix, float matrix[16]);
// - Env map reflection (FFP_GenerateTexCoord_EnvMap_Reflect): the guest reflects the eye direction
//   about the (not normalised) normal in its view space and takes the result back to world space
//   with z negated: S * reflect(eye_world, n_world), S = diag(1, 1, -1), then its texture matrix *
//   (r, 1). The host's world is the guest view space (world = guest world-view R * W), so the
//   backend reflects there (r_host = R3 * r_world) and this matrix takes r_host to the guest's
//   result: texture * [S * R3^T, 0; 0, 1], with R = world_view * guest_world^-1. Returns false when
//   guest_world is singular.
bool ReflectionMatrixForHost(const float world_view[16], const float guest_world[16],
                             const float* guest_texture_matrix, float matrix[16]);

// Runic renders with inverted depth (viewport MinZ 1 / MaxZ 0, clear with 1 - depth, swapped
// compare functions). The backend uses standard depth: it takes the compare function and the clear
// depth OGRE requested, before the guest's inversion.
uint8_t DepthFunctionForHost(uint8_t requested, uint8_t effective);
float ClearDepthForHost(float requested);

// Constant depth bias as OGRE requested it, for the host render system's _setDepthBias. GL3+
// passes it as glPolygonOffset units; Direct3D 11 multiplies it by 10 first (nearFarFactor,
// D3D11RenderSystem::_setDepthBias) and takes the result as DepthBias units. Both units are the
// depth buffer's smallest step (D24S8 on both), so the backend divides by the render system's
// factor (kD3D11DepthBiasFactor, 1 on GL3+). The guest's own bias semantics (its _setDepthBias
// with the inverted depth) are not reproduced; that would change GL3+ too.
constexpr float kD3D11DepthBiasFactor = 10.0f;
float DepthBiasConstantForHost(float requested, float render_system_factor);

// --- Textures ----------------------------------------------------------------------------------

// Block-compressed (DXT) textures keep only their mip levels of at least 4x4 texels; the levels
// smaller than one block in either direction are dropped, on every render system. Direct3D 11
// uploads those levels wrong through OGRE and drops the last two anyway (D3D11Texture::
// _create2DTex), and GL3+ with the same cut gets closer to the Xenos captures (v14 swap1700 +0.08
// dB, v19town swap2018 +0.16 dB), so the console does not seem to sample them either. Returns
// how many mip levels below the top one to keep, of the `mipmaps` the image has.
uint32_t CompressedMipmapsForHost(uint32_t width, uint32_t height, uint32_t mipmaps);

// --- Lighting and fog -------------------------------------------------------------------------

// The guest RTSS uploads, per directional light, the view-space direction towards the light
// (light_position_view_space). OGRE lights take the direction the light travels.
void DirectionalLightForHost(const float towards_light_view[3], float direction[3]);

// Guest linear fog (FFPLib FFP_PixelFog_Linear, reproduced by the backend's GuestPixelFog with
// the clip-space w as distance) gets (end, 1 / (end - start)) from the guest; OGRE's fog
// parameters take start and end.
void LinearFogRangeForHost(float guest_end, float guest_inverse_range, float* start, float* end);

// --- Display gamma ---------------------------------------------------------------------------

// The Xbox applies a display gamma ramp at scanout, written by D3D when it presents
// (commands::SetGammaRamp; the reference image ReXGlue's presenter gives already includes it).
// Builds the host LUT for 8-bit colour: lut[v * 3 + channel] for v in 0..255, the same formulas as
// the ReXGlue presenter:
// - Table: out = (value16 >> 6) / 1023 (10-bit DC_LUT_30_COLOR entries, indexed by the 8-bit
//   value).
// - PWL: s = the value as 10 bits; entry = s >> 3; out = (base + (s & 7) * delta / 8) / 65472 with
//   the low 6 bits of base and delta forced to 0 (DC_LUT_PWL_DATA bits 0:5 are hardwired zero).
// With no ramp written the hardware default the presenter uses is linear.
void GammaRampLut(bool pwl, const uint16_t values[768], uint8_t lut[256 * 3]);

// --- Rasterisation ----------------------------------------------------------------------------

// Pixel centres: Xenos under the guest's D3D samples pixel (i, j) at window position (i, j), as
// D3D9 does; GL samples it at (i + 0.5, j + 0.5). The guest compensates for D3D9 itself (its UI
// quads span [-0.5, size - 0.5]), so drawn as is on GL every primitive lands half a pixel up and to
// the left and the last row and column of a full-screen quad stay uncovered. Moves the guest clip
// matrix half a pixel right and down (D3D window y grows downwards) for a viewport of the given
// size in pixels: row0 += row3 / width, row1 -= row3 / height. Input and output in guest clip
// conventions (before GuestClipToGl and before any render-target y flip).
void D3DPixelCentresToGl(float clip[16], int viewport_width, int viewport_height);

// --- Host render systems (GL3+, Direct3D 11) ---------------------------------------------------

// What differs between the host render systems is handled by OGRE through its API, which the
// backend uses for every guest draw: the projection's depth range (RenderSystem::
// _convertProjectionMatrix: [-1, 1] on GL, [0, 1] on D3D11; every clip matrix above is in GL form
// and goes through it), the render-texture y flip (RenderTarget::requiresTextureFlipping: GL
// stores render textures bottom up, D3D11 top down; the backend negates clip y where it is set),
// read-backs (copyContentsToMemory), scissors and viewports (from the top on both). Clears differ
// in area (GL3+ the viewport's, Direct3D 11 the whole target): measured once at creation, see
// ClearViewport in backend.cpp. Pixel centres
// are the same on both (i + 0.5), so D3DPixelCentresToGl applies to both. The vertex colour
// layout is the same (VET_UBYTE4_NORM: R, G, B, A bytes on both). The one thing the backend draws
// outside that is its full-screen copy between render textures, which must keep memory order (the
// destination's row r takes the source's row r):
// Full-screen quad for that copy, as 4 vertices of (clip x, clip y, u, v) in triangle-strip order
// (top left, top right, bottom left, bottom right). The top edge (clip y = +1) lands on the
// destination's last row in memory where render textures are flipped (GL) and on its first row
// where they are not (D3D11), and texture v = 0 is the first row in memory on both: the top edge
// samples v = 1 or v = 0 accordingly.
void RenderTextureCopyQuad(bool destination_flips, float quad[16]);

}  // namespace backend
}  // namespace torchlight

#endif
