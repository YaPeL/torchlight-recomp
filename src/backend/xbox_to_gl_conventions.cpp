#include "backend/xbox_to_gl_conventions.h"

#include <string.h>

#include <bit>

namespace torchlight {
namespace backend {

// Word at a time (memcpy keeps it alignment-safe; the compilers vectorise these loops). Bytes
// past the last whole word stay as they are.
template <typename Word, Word (*Swap)(Word)>
void SwapWords(uint8_t* d, size_t size) {
  for (size_t i = 0; i + sizeof(Word) <= size; i += sizeof(Word)) {
    Word w;
    memcpy(&w, d + i, sizeof(Word));
    w = Swap(w);
    memcpy(d + i, &w, sizeof(Word));
  }
}
uint16_t Swap8In16(uint16_t w) { return std::byteswap(w); }
uint32_t Swap8In32(uint32_t w) { return std::byteswap(w); }
uint32_t Swap16In32(uint32_t w) { return std::rotl(w, 16); }

void SwapFetchEndian(uint8_t* d, size_t size, uint32_t mode) {
  switch (mode) {
    case kEndian8In16:
      SwapWords<uint16_t, Swap8In16>(d, size);
      break;
    case kEndian8In32:
      SwapWords<uint32_t, Swap8In32>(d, size);
      break;
    case kEndian16In32:
      SwapWords<uint32_t, Swap16In32>(d, size);
      break;
    default:
      break;
  }
}

void SwapIndices(uint8_t* data, size_t size, uint32_t index_size) {
  SwapFetchEndian(data, size, index_size == 4 ? kEndian8In32 : kEndian8In16);
}

bool BlendLanesForHost(uint8_t* data, size_t size, uint32_t stride, uint32_t index_offset,
                       uint32_t weight_offset, uint32_t weight_components, uint32_t count,
                       const uint8_t index_lane[4], const uint8_t weight_lane[4]) {
  if (count > weight_components || count > 4 || stride == 0) return false;
  if (index_offset + 4 > stride || weight_offset + 4 * weight_components > stride) return false;
  for (size_t v = 0; v + stride <= size; v += stride) {
    uint8_t* indices = data + v + index_offset;
    uint8_t* weights = data + v + weight_offset;
    uint8_t in_indices[4];
    float in_weights[4];
    memcpy(in_indices, indices, 4);
    for (uint32_t lane = 0; lane < 4; ++lane) {
      if (lane < weight_components) {
        memcpy(&in_weights[lane], weights + 4 * lane, 4);
      } else {
        in_weights[lane] = MissingVertexComponent(lane);
      }
    }
    float out_weights[4] = {0, 0, 0, 0}, sum = 0;
    uint8_t out_indices[4] = {0, 0, 0, 0};
    for (uint32_t k = 0; k < count; ++k) {
      out_indices[k] = in_indices[index_lane[k] & 3];
      out_weights[k] = in_weights[weight_lane[k] & 3];
      sum += out_weights[k];
    }
    if (sum != 0)
      for (uint32_t k = 0; k < count; ++k) out_weights[k] /= sum;
    memcpy(indices, out_indices, 4);
    for (uint32_t lane = 0; lane < weight_components; ++lane)
      memcpy(weights + 4 * lane, &out_weights[lane], 4);
  }
  return true;
}

void VertexColourD3DToGl(uint8_t* c) {
  uint8_t b = c[0];
  c[0] = c[2];  // R
  c[2] = b;     // B
}

void GuestClipToGl(const float g[16], float gl[16]) {
  for (int i = 0; i < 16; ++i) gl[i] = g[i];
  for (int j = 0; j < 4; ++j) gl[8 + j] = 2.0f * g[8 + j] - g[12 + j];  // depth [0,1] -> [-1,1]
}

void D3DPixelCentresToGl(float clip[16], int viewport_width, int viewport_height) {
  if (viewport_width <= 0 || viewport_height <= 0) return;
  for (int j = 0; j < 4; ++j) {
    clip[0 + j] += clip[12 + j] / float(viewport_width);
    clip[4 + j] -= clip[12 + j] / float(viewport_height);
  }
}

namespace {
void Multiply(const float a[16], const float b[16], float r[16]) {
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j) {
      float sum = 0;
      for (int k = 0; k < 4; ++k) sum += a[i * 4 + k] * b[k * 4 + j];
      r[i * 4 + j] = sum;
    }
}
}  // namespace

void GuestFixedFunctionClip(const float world[16], const float view[16],
                            const float projection[16], float clip[16]) {
  float left_handed_view[16];
  for (int i = 0; i < 16; ++i) left_handed_view[i] = (i / 4 == 2) ? -view[i] : view[i];
  float vw[16];
  Multiply(left_handed_view, world, vw);
  Multiply(projection, vw, clip);
}

namespace {
bool Invert(const float m[16], float out[16]) {
  float inv[16];
  inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] +
           m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
  inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] -
           m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
  inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] +
           m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
  inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] -
            m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
  inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] -
           m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
  inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] +
           m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
  inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] -
           m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
  inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] +
            m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
  inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] +
           m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
  inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] -
           m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
  inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] +
            m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
  inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] -
            m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
  inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] -
           m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
  inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] +
           m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
  inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] -
            m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
  inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] +
            m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
  float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
  if (det == 0.0f || det != det) return false;
  for (int i = 0; i < 16; ++i) out[i] = inv[i] / det;
  return true;
}

float Abs(float x) { return x < 0 ? -x : x; }
}  // namespace

bool GuestWorldViewFromWvp(const float wvp[16], const float projection[16], float world_view[16],
                           float projection_with_flip[16]) {
  for (int i = 0; i < 16; ++i) projection_with_flip[i] = (i % 4 == 2) ? -projection[i] : projection[i];
  float inverse[16];
  if (!Invert(projection_with_flip, inverse)) return false;
  Multiply(inverse, wvp, world_view);
  // Affine: bottom row 0 0 0 1, relative to the matrix scale.
  float scale = 0;
  for (int i = 0; i < 12; ++i) scale = Abs(world_view[i]) > scale ? Abs(world_view[i]) : scale;
  float tolerance = 1e-3f * (scale > 1 ? scale : 1);
  return Abs(world_view[12]) < tolerance && Abs(world_view[13]) < tolerance &&
         Abs(world_view[14]) < tolerance && Abs(world_view[15] - 1) < 1e-3f;
}

void SphereMapMatrixForHost(const float* guest_texture_matrix, float matrix[16]) {
  // (s, t, 0, 0) = A * (n, 1).
  const float a[16] = {0.5f, 0, 0, 0.5f, 0, -0.5f, 0, 0.5f, 0, 0, 0, 0, 0, 0, 0, 0};
  if (guest_texture_matrix) {
    Multiply(guest_texture_matrix, a, matrix);
  } else {
    for (int i = 0; i < 16; ++i) matrix[i] = a[i];
  }
  matrix[12] = matrix[13] = matrix[14] = 0;
  matrix[15] = 1;
}

bool ReflectionMatrixForHost(const float world_view[16], const float guest_world[16],
                             const float* guest_texture_matrix, float matrix[16]) {
  float inverse_world[16], view[16];
  if (!Invert(guest_world, inverse_world)) return false;
  Multiply(world_view, inverse_world, view);
  float back[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) back[i * 4 + j] = (i == 2 ? -1.0f : 1.0f) * view[j * 4 + i];
  if (guest_texture_matrix) {
    Multiply(guest_texture_matrix, back, matrix);
  } else {
    for (int i = 0; i < 16; ++i) matrix[i] = back[i];
  }
  return true;
}

void GammaRampLut(bool pwl, const uint16_t values[768], uint8_t lut[256 * 3]) {
  for (int v = 0; v < 256; ++v) {
    for (int ch = 0; ch < 3; ++ch) {
      float out;
      if (pwl) {
        int s10 = (v * 1023 + 127) / 255;
        int entry = s10 >> 3;
        float base = float(values[ch * 256 + entry * 2] & 0xFFC0);
        float delta = float(values[ch * 256 + entry * 2 + 1] & 0xFFC0);
        out = (base + float(s10 & 7) * delta * 0.125f) / 65472.0f;
      } else {
        out = float(values[ch * 256 + v] >> 6) / 1023.0f;
      }
      if (out < 0) out = 0;
      if (out > 1) out = 1;
      lut[v * 3 + ch] = uint8_t(out * 255.0f + 0.5f);
    }
  }
}

void DirectionalLightForHost(const float towards_light_view[3], float direction[3]) {
  for (int i = 0; i < 3; ++i) direction[i] = -towards_light_view[i];
}

void LinearFogRangeForHost(float guest_end, float guest_inverse_range, float* start, float* end) {
  *end = guest_end;
  *start = guest_inverse_range != 0 ? guest_end - 1.0f / guest_inverse_range : guest_end;
}

uint8_t CullModeForHost(uint8_t guest_cull_mode, bool invert_winding) {
  (void)invert_winding;  // passed to OGRE's setInvertVertexWinding by the backend
  return guest_cull_mode;
}

uint8_t DepthFunctionForHost(uint8_t requested, uint8_t effective) {
  (void)effective;  // the guest's inverted function only matches its inverted depth range
  return requested;
}

float ClearDepthForHost(float requested) { return requested; }

float DepthBiasConstantForHost(float requested, float render_system_factor) {
  return requested / render_system_factor;
}

uint32_t CompressedMipmapsForHost(uint32_t width, uint32_t height, uint32_t mipmaps) {
  uint32_t keep = 0;
  for (uint32_t side = width < height ? width : height; side >= 8 && keep < mipmaps; side >>= 1) {
    ++keep;
  }
  return keep;
}

void RenderTextureCopyQuad(bool destination_flips, float quad[16]) {
  const float top_v = destination_flips ? 1.0f : 0.0f;
  const float bottom_v = 1.0f - top_v;
  const float vertices[16] = {-1, 1, 0, top_v,    1, 1, 1, top_v,
                              -1, -1, 0, bottom_v, 1, -1, 1, bottom_v};
  for (int i = 0; i < 16; ++i) quad[i] = vertices[i];
}

}  // namespace backend
}  // namespace torchlight
