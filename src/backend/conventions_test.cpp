// Tests for the parts of xbox_to_gl_conventions that do not need OGRE.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <initializer_list>

#include "backend/xbox_to_gl_conventions.h"

namespace {

void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    std::exit(1);
  }
}

int Abs(int x) { return x < 0 ? -x : x; }

}  // namespace

namespace {
void Mul(const float* a, const float* b, float* r) {
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j) {
      float v = 0;
      for (int k = 0; k < 4; ++k) v += a[i * 4 + k] * b[k * 4 + j];
      r[i * 4 + j] = v;
    }
}
void Apply(const float* m, const float v[4], float out[4]) {
  for (int i = 0; i < 4; ++i) out[i] = m[i * 4] * v[0] + m[i * 4 + 1] * v[1] + m[i * 4 + 2] * v[2] + m[i * 4 + 3] * v[3];
}
void Apply3(const float* m, const float v[3], float out[3]) {  // (float3x3)m * v
  for (int i = 0; i < 3; ++i) out[i] = m[i * 4] * v[0] + m[i * 4 + 1] * v[1] + m[i * 4 + 2] * v[2];
}
void Reflect(const float i[3], const float n[3], float r[3]) {
  float d = i[0] * n[0] + i[1] * n[1] + i[2] * n[2];
  for (int k = 0; k < 3; ++k) r[k] = i[k] - 2 * d * n[k];
}
void Normalize(float v[3]) {
  float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  for (int k = 0; k < 3; ++k) v[k] /= l;
}
}  // namespace

int main() {
  {
    // Skinning lanes: Runic's blend (index lane 3 - k with weight lane k, position divided by the
    // weight sum through w) against the RTSS's (lane k with lane k, w = 1) after BlendLanesForHost.
    using torchlight::backend::BlendLanesForHost;
    const float bones[3][12] = {{1, 0, 0, 1, 0, 1, 0, 0, 0, 0, 1, 0},
                                {0, -1, 0, 0, 1, 0, 0, 2, 0, 0, 1, -3},
                                {2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 5}};
    // Vertex: float3 position, UBYTE4 indices, FLOAT2 weights (24 bytes).
    unsigned char v[24];
    const float p[3] = {0.5f, -1.0f, 2.0f}, w[2] = {0.3f, 0.5f};
    const unsigned char idx[4] = {0, 0, 1, 2};  // Runic reads lane 3 (bone 2) then lane 2 (bone 1)
    std::memcpy(v, p, 12);
    std::memcpy(v + 12, idx, 4);
    std::memcpy(v + 16, w, 8);
    const uint8_t index_lane[4] = {3, 2, 0, 0}, weight_lane[4] = {0, 1, 0, 0};
    float acc[4] = {0, 0, 0, 0};
    for (int k = 0; k < 2; ++k) {
      const float* m = bones[idx[index_lane[k]]];
      float wk = w[weight_lane[k]];
      for (int r = 0; r < 3; ++r) acc[r] += wk * (m[r * 4] * p[0] + m[r * 4 + 1] * p[1] + m[r * 4 + 2] * p[2] + m[r * 4 + 3]);
      acc[3] += wk;
    }
    Check(BlendLanesForHost(v, sizeof(v), 24, 12, 16, 2, 2, index_lane, weight_lane),
          "skinning lanes: two influences fit FLOAT2 weights");
    float host[3] = {0, 0, 0}, hw[2];
    std::memcpy(hw, v + 16, 8);
    for (int k = 0; k < 2; ++k) {
      const float* m = bones[v[12 + k]];
      for (int r = 0; r < 3; ++r) host[r] += hw[k] * (m[r * 4] * p[0] + m[r * 4 + 1] * p[1] + m[r * 4 + 2] * p[2] + m[r * 4 + 3]);
    }
    bool same = true;
    for (int r = 0; r < 3; ++r) same &= std::fabs(host[r] - acc[r] / acc[3]) < 1e-5f;
    Check(same, "skinning lanes: host blend matches Runic's");
    Check(v[14] == 0 && v[15] == 0, "skinning lanes: unused index lanes zeroed");
    Check(!BlendLanesForHost(v, sizeof(v), 24, 12, 16, 2, 3, index_lane, weight_lane),
          "skinning lanes: three influences do not fit FLOAT2 weights");
  }
  {
    // Reflection texgen: the guest's FFP_GenerateTexCoord_EnvMap_Reflect (7-argument form), step by
    // step, against the host path (reflect in the host world = guest view space, then the matrix).
    const float c = std::cos(0.6f), sn = std::sin(0.6f), c2 = std::cos(-1.1f), s2 = std::sin(-1.1f);
    const float scale = 2.5f;
    // World: rotation about y, uniform scale, translation.
    float world[16] = {scale * c, 0, scale * sn, 3, 0, scale, 0, -1, -scale * sn, 0, scale * c, 7, 0, 0, 0, 1};
    float world_it[16] = {c / scale, 0, sn / scale, 0, 0, 1 / scale, 0, 0, -sn / scale, 0, c / scale, 0, 0, 0, 0, 1};
    // View: rotation about x and translation (right-handed OGRE view).
    float view[16] = {1, 0, 0, -2, 0, c2, -s2, 4, 0, s2, c2, -30, 0, 0, 0, 1};
    float texture[16] = {0.5f, 0, 0, 0.1f, 0, -1, 0, 0.2f, 0, 0, 2, -0.3f, 0, 0, 0, 1};
    const float pos[4] = {0.3f, 1.2f, -0.7f, 1}, normal[3] = {0.2f, 0.9f, -0.4f};
    // Guest.
    float g_view[16];
    for (int i = 0; i < 16; ++i) g_view[i] = view[i];
    for (int j = 0; j < 4; ++j) g_view[8 + j] = -g_view[8 + j];
    float view_t[16];
    for (int i = 0; i < 4; ++i)
      for (int j = 0; j < 4; ++j) view_t[i * 4 + j] = g_view[j * 4 + i];
    float n_world[3], n_view[3], p_world[4], p_view[4], e[3], r[3], r_world[3];
    Apply3(world_it, normal, n_world);
    Apply3(g_view, n_world, n_view);
    Apply(world, pos, p_world);
    Apply(g_view, p_world, p_view);
    for (int k = 0; k < 3; ++k) e[k] = p_view[k];
    Normalize(e);
    Reflect(e, n_view, r);
    for (int j = 0; j < 4; ++j) view_t[8 + j] = j < 3 ? -view_t[8 + j] : view_t[8 + j];
    Apply3(view_t, r, r_world);
    const float r4[4] = {r_world[0], r_world[1], r_world[2], 1};
    float guest[4];
    Apply(texture, r4, guest);
    // Host: world = guest world-view; normal matrix = its inverse transpose (view is orthonormal).
    float world_view[16], host_it[16];
    Mul(view, world, world_view);
    Mul(view, world_it, host_it);
    float nh[3], ph[4], eh[3], rh[3];
    Apply3(host_it, normal, nh);
    Apply(world_view, pos, ph);
    for (int k = 0; k < 3; ++k) eh[k] = ph[k];
    Normalize(eh);
    Reflect(eh, nh, rh);
    float m[16];
    Check(torchlight::backend::ReflectionMatrixForHost(world_view, world, texture, m),
          "reflection: invertible world");
    const float rh4[4] = {rh[0], rh[1], rh[2], 1};
    float host[4];
    Apply(m, rh4, host);
    bool same = true;
    for (int k = 0; k < 3; ++k) same &= std::fabs(host[k] - guest[k]) < 1e-4f;
    Check(same, "reflection: host path matches the guest's FFP_GenerateTexCoord_EnvMap_Reflect");
  }
  {
    // A full-screen quad as the guest draws it on a 4x2 viewport, spanning [-0.5, size - 0.5] in
    // D3D window pixels, covers [0, size] in GL window pixels after the conversion.
    using torchlight::backend::D3DPixelCentresToGl;
    float clip[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 2};  // w = 2
    D3DPixelCentresToGl(clip, 4, 2);
    // Vertex at the D3D window's top-left (-0.5, -0.5): ndc (-1.25, 1.5), as clip with w = 2.
    float v[4] = {-2.5f, 3.0f, 0, 1};
    float x = clip[0] * v[0] + clip[1] * v[1] + clip[2] * v[2] + clip[3] * v[3];
    float y = clip[4] * v[0] + clip[5] * v[1] + clip[6] * v[2] + clip[7] * v[3];
    float w = clip[12] * v[0] + clip[13] * v[1] + clip[14] * v[2] + clip[15] * v[3];
    float gl_x = (x / w + 1) * 0.5f * 4;  // GL window x
    float gl_y = (y / w + 1) * 0.5f * 2;  // GL window y (bottom-up)
    Check(gl_x > -1e-5f && gl_x < 1e-5f, "pixel centres: left edge at GL x = 0");
    Check(gl_y > 2 - 1e-5f && gl_y < 2 + 1e-5f, "pixel centres: top edge at GL y = height");
  }
  {
    // The full-screen copy between render textures keeps memory order on both render systems.
    using torchlight::backend::RenderTextureCopyQuad;
    float gl[16], d3d[16];
    RenderTextureCopyQuad(true, gl);
    RenderTextureCopyQuad(false, d3d);
    const float before[16] = {-1, 1, 0, 1, 1, 1, 1, 1, -1, -1, 0, 0, 1, -1, 1, 0};
    bool unchanged = true;
    for (int i = 0; i < 16; ++i) unchanged &= gl[i] == before[i];
    Check(unchanged, "render texture copy: the GL quad is the one the backend always used");
    // Destination row in memory (of `rows`) a clip y lands on, and source row a v samples.
    const int rows = 8;
    auto row_from_clip = [&](bool flips, float y) {
      float top_down = (1 - y) * 0.5f * rows;  // 0 at the top edge
      return int(flips ? rows - top_down : top_down);
    };
    auto row_from_v = [&](float v) { return int(v * rows); };
    for (bool flips : {true, false}) {
      const float* q = flips ? gl : d3d;
      // Just inside the top edge and the bottom edge (vertices 0 and 2 carry y = +1 and -1).
      const float e = 0.01f;
      int top_dst = row_from_clip(flips, 1 - e), bottom_dst = row_from_clip(flips, -1 + e);
      int top_src = row_from_v(q[3] + (q[11] - q[3]) * e * 0.5f);
      int bottom_src = row_from_v(q[11] + (q[3] - q[11]) * e * 0.5f);
      if (top_src == rows) top_src = rows - 1;
      if (bottom_src == rows) bottom_src = rows - 1;
      if (top_dst == rows) top_dst = rows - 1;
      if (bottom_dst == rows) bottom_dst = rows - 1;
      Check(top_dst == top_src && bottom_dst == bottom_src,
            flips ? "render texture copy: memory order on flipped targets (GL)"
                  : "render texture copy: memory order on unflipped targets (D3D11)");
    }
  }
  using torchlight::backend::GammaRampLut;
  uint16_t values[768];
  uint8_t lut[256 * 3];

  // Linear 256-entry table: identity.
  for (int ch = 0; ch < 3; ++ch)
    for (int v = 0; v < 256; ++v) values[ch * 256 + v] = uint16_t(v * 65535 / 255);
  GammaRampLut(false, values, lut);
  bool identity = true;
  for (int i = 0; i < 256 * 3; ++i) identity &= lut[i] == i / 3;
  Check(identity, "linear table is the identity");

  // Linear PWL as the guest builds it (sub_82776730): base_i = i * 0xFFFF / 127,
  // delta_i = (i + 1) * 0xFFFF / 127 - base_i (the last delta covers the top segment).
  for (int ch = 0; ch < 3; ++ch)
    for (int i = 0; i < 128; ++i) {
      uint32_t base = uint32_t(i) * 0xFFFF / 127;
      uint32_t next = uint32_t(i + 1) * 0xFFFF / 127;
      values[ch * 256 + i * 2] = uint16_t(base);
      values[ch * 256 + i * 2 + 1] = uint16_t(next - base);
    }
  GammaRampLut(true, values, lut);
  int worst = 0;
  for (int i = 0; i < 256 * 3; ++i) worst = Abs(lut[i] - i / 3) > worst ? Abs(lut[i] - i / 3) : worst;
  Check(worst <= 2, "linear PWL is the identity within 2 / 255");

  // A table with a black floor lifts black.
  for (int ch = 0; ch < 3; ++ch)
    for (int v = 0; v < 256; ++v) values[ch * 256 + v] = uint16_t((12 + v * 243 / 255) * 257);
  GammaRampLut(false, values, lut);
  Check(lut[0] == 12 && lut[255 * 3] == 255, "black floor");

  {
    // Compressed mip chains stop at the last level of at least 4x4 texels.
    using torchlight::backend::CompressedMipmapsForHost;
    Check(CompressedMipmapsForHost(128, 32, 7) == 3, "128x32: down to 16x4");
    Check(CompressedMipmapsForHost(64, 64, 6) == 4, "64x64: down to 4x4");
    Check(CompressedMipmapsForHost(64, 64, 2) == 2, "never more than the image has");
    Check(CompressedMipmapsForHost(256, 4, 8) == 0, "a side of 4: the top level only");
    Check(CompressedMipmapsForHost(4, 4, 0) == 0, "no mipmaps");
  }
  {
    // The depth bias reaches both render systems in the same units.
    using namespace torchlight::backend;
    Check(DepthBiasConstantForHost(500, 1) == 500, "GL3+ takes the bias as OGRE requested it");
    Check(DepthBiasConstantForHost(500, kD3D11DepthBiasFactor) * kD3D11DepthBiasFactor == 500,
          "Direct3D 11 ends up with the requested units");
  }

  std::printf("conventions test: ok\n");
  return 0;
}
