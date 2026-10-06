/* C boundary of the host render backend (OGRE 14 GL, src/backend). Only plain C types cross this
 * boundary. Values are the neutral command values (see src/commands/types.h);
 * guest data keeps the guest byte order and the backend converts it in one place
 * (xbox_to_gl_conventions). */

#ifndef TORCHLIGHT_BACKEND_API_H
#define TORCHLIGHT_BACKEND_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tl_backend tl_backend;

/* The OGRE render system the backend draws with. Direct3D 11 exists on Windows only; creating a
 * backend with a render system whose plugin is not there fails with the reason. */
typedef enum tl_render_system {
  TL_RENDER_SYSTEM_GL3PLUS = 0,
  TL_RENDER_SYSTEM_D3D11 = 1,
} tl_render_system;

/* Backend rendering to a width x height colour target with `render_system`, on the GPU `gpu` (a
 * platform GPU id, platform/platform.h; null or empty: automatic). Only Direct3D 11 takes a GPU
 * (OGRE's "Rendering Device", platform::RenderingDeviceForGpu); one it cannot find stays automatic,
 * said in OGRE's log. OGRE's plugins and media come from the platform's directories
 * (platform::OgrePluginDir, OgreMediaDir); OGRE's log goes to `log_path`, or only to the debugger
 * output with null. With a `window_title` it also opens a window of that size that
 * tl_backend_present shows the frame's output in; with null it is offscreen only. Returns null and
 * fills `error` on failure. All calls must come from the thread that created it (the render
 * system's context lives there). */
tl_backend* tl_backend_create(tl_render_system render_system, const char* gpu, uint32_t width,
                              uint32_t height, const char* log_path, const char* window_title,
                              char* error, uint32_t error_size);
/* Another application's window, as platform/platform.h defines it (NativeWindow: same fields and
 * values): the window system, the window and, where the system needs it, its display. */
typedef struct tl_native_window {
  uint32_t system;
  uint64_t window;
  uint64_t display;
} tl_native_window;
/* Same, but drawing inside another application's window (`parent`), at window_width x
 * window_height to start with: a child window on X11, the window itself on Wayland (its surface)
 * and on Win32 (OGRE takes the HWND as its own). The application keeps the focus and its events.
 * The output is shown letterboxed in it. With `vsync`, presenting waits for the display's vertical
 * sync. */
tl_backend* tl_backend_create_child(tl_render_system render_system, const char* gpu,
                                    uint32_t width, uint32_t height, const char* log_path,
                                    const tl_native_window* parent, uint32_t window_width,
                                    uint32_t window_height, int vsync, char* error,
                                    uint32_t error_size);
void tl_backend_destroy(tl_backend* b);

/* "renderer | vendor | version" as reported through OGRE. */
const char* tl_backend_renderer(tl_backend* b);

/* ---- resources (ids are capture resource identities packed by the caller) ---- */
/* Creating a texture or render target may replace the texture bound on a unit (OGRE GL binds
 * while it uploads): create every texture a draw uses before binding its units. */

/* Vertex buffer bytes exactly as the guest stored them; `fetch_endian` is the 2-bit swap mode of
 * the vertex fetch constant (vertex data is converted per declaration at draw time). */
int tl_backend_vertex_buffer(tl_backend* b, uint64_t id, const void* data, uint32_t size,
                             uint32_t fetch_endian);
/* Index buffer bytes in guest CPU order (big-endian), 2 or 4 bytes per index. */
int tl_backend_index_buffer(tl_backend* b, uint64_t id, const void* data, uint32_t size,
                            uint32_t index_size);
enum tl_texture_type { TL_TEXTURE_2D = 0, TL_TEXTURE_CUBE = 1 };
/* Texture from an image file (e.g. a DDS from the game data). */
int tl_backend_texture_file(tl_backend* b, uint64_t id, const char* name, const void* data,
                            uint32_t size, const char* extension, uint32_t type, char* error,
                            uint32_t error_size);

enum tl_linear_format {
  TL_LINEAR_BGRA8 = 0, /* bytes B, G, R, A (D3D A8R8G8B8 read as a little-endian dword) */
  TL_LINEAR_DXT1 = 1,
  TL_LINEAR_DXT3 = 2,
  TL_LINEAR_DXT5 = 3
};
/* Texture already untiled and in host order (base level only). An existing texture with this id
 * (not a render target) is rewritten in place when the size and format match, replaced otherwise:
 * the guest's dynamic textures change content every few frames. */
int tl_backend_texture_linear(tl_backend* b, uint64_t id, uint32_t width, uint32_t height,
                              uint32_t format, const void* data, uint32_t size);
/* Render target texture; also usable as a texture with the same id. */
int tl_backend_render_target(tl_backend* b, uint64_t id, uint32_t width, uint32_t height);
/* Frees a vertex or index buffer (a superseded content version) / a texture or render target. */
void tl_backend_release_buffer(tl_backend* b, uint64_t id);
/* The guest buffer `owner` (tl_draw stream_owners / index_owner) was destroyed: frees the host
 * buffers kept for it. */
void tl_backend_release_buffer_owner(tl_backend* b, uint64_t owner);
void tl_backend_release_texture(tl_backend* b, uint64_t id);

/* ---- frame ---- */

void tl_backend_begin(tl_backend* b);
void tl_backend_end(tl_backend* b);
/* Shows the frame's output (after tl_backend_end) in the window and processes its events.
 * Returns 0 when there is no window or it was closed. */
int tl_backend_present(tl_backend* b);
/* Keys pressed in the backend's top-level window since the last call (tl_key bits), read when
 * presenting. A child window never receives keys. */
enum tl_key { TL_KEY_F9 = 1 };
uint32_t tl_backend_take_keys(tl_backend* b);
/* Vertical sync of a visible window, while running (the window the backend presents in). */
void tl_backend_set_vsync(tl_backend* b, int vsync);
/* A window inside another application's (tl_backend_create_child): follows its new size. No-op
 * for other windows. */
void tl_backend_resize_window(tl_backend* b, uint32_t width, uint32_t height);
/* Internal render scale (0.25 to 4; 1 to start with): the main target and the output are drawn at
 * `scale` times the guest's size; the guest's render targets keep their sizes (it samples them as
 * textures). Everything else stays in the guest's pixels, and reads return guest sizes. Call
 * between frames: the main target and output are recreated, losing their content. Returns 0 on
 * success. */
int tl_backend_set_render_scale(tl_backend* b, float scale);
/* The guest's frame size (its window target): the main target and the output are recreated at
 * width x height (times the render scale), losing their content; viewports and reads use the new
 * size. The guest picks it when it creates its device (a fixed table of video modes), so it changes
 * at most a few times per session. Returns 0 on success (also when the size is unchanged). */
int tl_backend_set_guest_size(tl_backend* b, uint32_t width, uint32_t height);
/* The front end's 3D scene on a frame wider than 16:9 (commands SetSceneClip): while enabled, draws
 * and clears on the main target are limited to its centred 16:9 strip, and a clear turns the rest
 * of the target black. The camera keeps its vertical field of view, so that strip is the scene's
 * 16:9 view. No effect when the frame is not wider than 16:9. */
void tl_backend_set_scene_clip(tl_backend* b, int enabled);
/* Target 0 is the main colour target. */
int tl_backend_set_target(tl_backend* b, uint64_t target_id);
void tl_backend_set_viewport(tl_backend* b, int32_t left, int32_t top, int32_t width,
                             int32_t height);
/* Values as the guest requested them (before the guest's own depth inversion). */
void tl_backend_clear(tl_backend* b, uint32_t buffers, const float colour[4], float depth,
                      uint32_t stencil);

typedef struct tl_state {
  uint8_t blend_src, blend_dst, blend_op;       /* commands::BlendFactor / BlendOp */
  uint8_t depth_check, depth_write;
  uint8_t depth_func_requested;                 /* commands::CompareFunc, before inversion */
  uint8_t depth_func_effective;                 /* after the guest's inversion */
  uint8_t cull_mode;                            /* commands::CullMode */
  uint8_t invert_winding;
  uint8_t alpha_func;                           /* commands::CompareFunc */
  uint8_t alpha_ref;
  uint8_t colour_write;                         /* bit0 r, bit1 g, bit2 b, bit3 a */
  uint8_t polygon_mode;                         /* commands::PolygonMode */
  float depth_bias_constant, depth_bias_slope;
} tl_state;
void tl_backend_set_state(tl_backend* b, const tl_state* s);

typedef struct tl_sampler {
  uint8_t min_filter, mag_filter, mip_filter;   /* commands::Filter */
  uint8_t address_u, address_v, address_w;      /* commands::AddressMode */
  uint32_t max_anisotropy;
  uint32_t coord_set;                           /* texture coordinate set used by the unit */
} tl_sampler;
/* texture_id 0 disables the unit. */
int tl_backend_set_texture(tl_backend* b, uint32_t unit, uint64_t texture_id,
                           const tl_sampler* sampler);

typedef struct tl_vertex_element {
  uint32_t stream, offset, index;
  uint8_t type;      /* commands::VertexType */
  uint8_t semantic;  /* commands::VertexSemantic */
} tl_vertex_element;

/* Colour of a draw before texturing. */
enum tl_colour_source {
  TL_COLOUR_WHITE = 0,    /* constant white: the texture alone */
  TL_COLOUR_VERTEX = 1,   /* the vertex colour (white when the declaration has none) */
  TL_COLOUR_LIGHTING = 2  /* scene colour plus lights (tl_lighting) */
};

/* Guest per-vertex lighting (RTSS FFPLighting): colour = scene_colour + sum over lights of
 * diffuse * saturate(dot(normal_view, towards_light_view)). Values as the guest uploaded them. */
typedef struct tl_lighting {
  float scene_colour[4];
  uint32_t light_count; /* 0 or 1 */
  float towards_light_view[3]; /* view-space direction towards the light */
  float light_diffuse[3];
  uint8_t diffuse_from_vertex; /* the light diffuse is modulated by the vertex colour */
} tl_lighting;

/* Guest linear fog (FFPLib FFP_PixelFog_Linear): factor = saturate((end - d) * inverse_range)
 * with d the clip-space w, colour = lerp(fog colour, colour, factor), alpha kept. */
typedef struct tl_fog {
  uint8_t enabled;
  float end, inverse_range;
  float colour[4];
} tl_fog;

/* Texture stage of a guest program (RTSS FFPTexturing): one colour and one alpha operation. */
enum tl_combine_op {
  TL_OP_SOURCE1 = 0, TL_OP_SOURCE2, TL_OP_MODULATE, TL_OP_MODULATE_X2, TL_OP_MODULATE_X4,
  TL_OP_ADD, TL_OP_ADD_SIGNED, TL_OP_ADD_SMOOTH, TL_OP_SUBTRACT
};
enum tl_combine_source {
  TL_SRC_TEXTURE = 0, /* this unit's texel */
  TL_SRC_CURRENT,     /* result of the previous stage (the colour before texturing for unit 0) */
  TL_SRC_DIFFUSE,     /* the colour before texturing */
  TL_SRC_CONSTANT
};
typedef struct tl_combine {
  uint8_t op, source1, source2;
  float constant1[4], constant2[4];
} tl_combine;
/* Texture coordinate generation (guest FFPLib functions, see rtss_program.h). */
enum tl_texgen {
  TL_TEXGEN_NONE = 0,       /* vertex texture coordinate set coord_set */
  TL_TEXGEN_PROJECTIVE,     /* projector * world * position, divided by w */
  TL_TEXGEN_VIEW_NORMAL,    /* view-space normal */
  TL_TEXGEN_SPHERE,         /* guest sphere map: (n.x / 2 + 0.5, -n.y / 2 + 0.5) of the view normal */
  TL_TEXGEN_REFLECT         /* guest reflection (FFP_GenerateTexCoord_EnvMap_Reflect): world-space
                               reflection of the eye direction, z negated; world = its world matrix */
};
#define TL_MAX_STAGES 8
typedef struct tl_stage {
  uint8_t sampled; /* the guest program samples this unit; others are disabled */
  tl_combine colour, alpha;
  uint8_t texgen;
  uint32_t coord_set;
  /* Texture matrix: TL_TEXGEN_NONE / VIEW_NORMAL: matrix * (coordinate, 1); TL_TEXGEN_SPHERE:
   * matrix * (s, t, 0, 0) as the guest applies it. Row-major, column vector. */
  uint8_t has_matrix;
  float matrix[16];
  float projector[16], world[16]; /* TL_TEXGEN_PROJECTIVE (and world for TL_TEXGEN_REFLECT): guest
                                     matrices as uploaded */
} tl_stage;

/* Runic hardware skinning, done on the GPU by the RTSS (BlendLanesForHost adapts the vertex
 * lanes). Positions and normals end up in world space; guest_wvp is then the guest's
 * view-projection. */
typedef struct tl_skinning {
  uint32_t influence_count; /* 0: not skinned */
  uint8_t index_lane[4], weight_lane[4];
  const float* bones; /* bone_count matrices, 3x4 row-major */
  uint32_t bone_count;
} tl_skinning;

typedef struct tl_draw {
  uint8_t primitive; /* commands::PrimitiveType */
  const tl_vertex_element* elements;
  uint32_t element_count;
  const uint64_t* stream_buffers; /* vertex buffer id per stream index (0 = none) */
  const uint32_t* stream_strides; /* vertex size per stream index */
  /* The guest buffer behind each stream (null or 0 = unknown: the content id stands for it). The
   * host keeps one GPU buffer per guest buffer and rewrites it when its content changes, instead
   * of one per content version. */
  const uint64_t* stream_owners;
  uint32_t stream_count;
  uint32_t vertex_start, vertex_count;
  uint64_t index_buffer; /* 0 = not indexed */
  uint64_t index_owner;  /* as stream_owners, for the index buffer */
  uint32_t index_start, index_count;
  /* With a guest vertex program: its world-view-projection exactly as uploaded (row-major, guest
   * clip conventions). Without one (fixed function): the matrices the guest passed to its world,
   * view and projection setters, and fixed_function = 1. */
  uint8_t fixed_function;
  float guest_wvp[16];
  float world[16], view[16], projection[16];
  /* With a guest vertex program: the matrix the guest passed to its projection setter. The
   * backend derives the view-space transform lighting and fog need from it and guest_wvp. */
  uint8_t has_guest_projection;
  float guest_projection[16];
  uint8_t colour_source; /* tl_colour_source */
  tl_lighting lighting;
  tl_fog fog;
  /* Texture stages read from the guest programs. When 0, unit 0 combines the texture with the
   * colour (or uses it alone for TL_COLOUR_WHITE) and further units are as set. */
  uint8_t program_stages;
  tl_stage stages[TL_MAX_STAGES];
  tl_skinning skinning;
} tl_draw;
/* Returns 0 on success, or a reason code (tl_skip_reason) when the draw was skipped. */
enum tl_skip_reason {
  TL_DRAWN = 0,
  TL_SKIP_UNKNOWN_VERTEX_TYPE = 1,
  TL_SKIP_MISSING_BUFFER = 2,
  TL_SKIP_UNKNOWN_PRIMITIVE = 3,
  TL_SKIP_BAD_RANGE = 4,
  TL_SKIP_OGRE_EXCEPTION = 5,
  TL_SKIP_NO_PROGRAM = 6, /* the RTSS could not generate the draw's programs */
  TL_SKIP_TOO_MANY_BONES = 7 /* the skinning palette does not fit the vertex program */
};
int tl_backend_draw(tl_backend* b, const tl_draw* d);
/* Approximations made by the last tl_backend_draw (tl_draw_note bits). */
enum tl_draw_note {
  /* The view-space transform could not be derived: lights and fog were dropped. */
  TL_NOTE_NO_VIEW_SPACE = 1,
  /* A stage combination the host fixed function cannot express (more than one constant). */
  TL_NOTE_STAGE_APPROXIMATED = 2,
  /* A guest stage beyond the host's fixed-function texture units was dropped. */
  TL_NOTE_STAGE_DROPPED = 4,
  /* Skinning requested but the declaration lacks float3 positions, UBYTE4 indices or float
   * weights: drawn unskinned. */
  TL_NOTE_SKINNING_LAYOUT = 8
};
uint32_t tl_backend_draw_notes(tl_backend* b);

/* Coverage probe (diagnostics): while enabled, every draw to the main target is drawn a second
 * time with colour and depth writes off, depth test off and a scissor on the rectangle (pixels,
 * right/bottom exclusive), counting its fragments with an occlusion query. The image is not
 * changed. tl_backend_probe_samples returns the count of the last draw (0 off the main target). */
void tl_backend_set_probe(tl_backend* b, int enabled, uint32_t left, uint32_t top, uint32_t right,
                          uint32_t bottom);
uint32_t tl_backend_probe_samples(tl_backend* b);

/* Work the backend did since the previous call, for the live mode's slow frame report: RTSS
 * programs generated and the time of the draws that generated them (generation, compilation and
 * the driver's work on first use, as far as it happens inside the draw), GPU buffers and vertex
 * layouts created. Resets the counts. */
typedef struct tl_backend_counters {
  uint32_t programs_generated;
  double program_ms;
  uint32_t gpu_buffers_created;
  uint32_t vertex_layouts_created;
} tl_backend_counters;
void tl_backend_take_counters(tl_backend* b, tl_backend_counters* out);

/* Display gamma ramp the guest set (commands::SetGammaRamp values), applied when the frame ends.
 * Linear until set. */
void tl_backend_set_gamma_ramp(tl_backend* b, int pwl, const uint16_t values[768]);

/* Reads the frame's output (main colour target after the display gamma) as RGBA8, top row first;
 * call after tl_backend_end. */
int tl_backend_read_rgba(tl_backend* b, void* rgba, uint32_t stride);
/* Reads a render target (tl_backend_render_target id) as RGBA8, top row first (diagnostics; call
 * after tl_backend_end). */
int tl_backend_read_target_rgba(tl_backend* b, uint64_t target_id, uint32_t width,
                                uint32_t height, void* rgba, uint32_t stride);

/* ---- host UI (the runtime's ImGui dialogs, not the guest's) ----
 * Drawn last, over the whole window (window coordinates, not the guest's frame: letterboxing does
 * not apply), when presenting; offscreen, over the output when the frame ends. Not part of the
 * guest's command stream or of captures. */

/* RGBA8 texture, top row first, by caller-chosen id (non-zero). Returns 0 on success. */
int tl_backend_ui_texture(tl_backend* b, uint64_t id, uint32_t width, uint32_t height, int linear,
                          int repeat, const uint8_t* rgba);
void tl_backend_ui_release_texture(tl_backend* b, uint64_t id);

typedef struct tl_ui_vertex {
  float x, y;      /* coordinate space units, origin at the top left */
  float u, v;
  uint32_t colour; /* RGBA8, red in the lowest byte (ImGui's ImU32) */
} tl_ui_vertex;
typedef struct tl_ui_cmd {
  uint64_t texture;     /* tl_backend_ui_texture id; 0 = colour only */
  uint32_t lines;       /* 0 triangles, 1 lines */
  uint32_t index_start; /* first index (indexed frames) or first vertex */
  uint32_t count;       /* indices (indexed frames) or vertices */
  int32_t base_vertex;
  int32_t scissor;      /* clip to the rectangle below (coordinate space units) */
  float scissor_left, scissor_top, scissor_right, scissor_bottom;
} tl_ui_cmd;
/* The UI drawn from now on (copied), until the next call; no commands = no UI. The coordinate
 * space (space_width x space_height) spans the whole window. `indices` may be null (commands draw
 * vertex ranges). */
void tl_backend_set_ui_frame(tl_backend* b, float space_width, float space_height,
                             const tl_ui_vertex* vertices, uint32_t vertex_count,
                             const uint16_t* indices, uint32_t index_count, const tl_ui_cmd* cmds,
                             uint32_t cmd_count);

#ifdef __cplusplus
}
#endif

#endif
